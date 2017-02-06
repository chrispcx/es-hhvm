/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <thrift/lib/cpp2/security/KerberosSASLHandshakeClient.h>

#include <gssapi/gssapi_generic.h>
#include <gssapi/gssapi_krb5.h>
#include <krb5.h>
#include <stdlib.h>
#include <folly/io/IOBuf.h>
#include <folly/io/Cursor.h>
#include <folly/Memory.h>
#include <folly/Singleton.h>
#include <thrift/lib/cpp/concurrency/Mutex.h>
#include <thrift/lib/cpp/util/kerberos/Krb5Util.h>
#include <thrift/lib/cpp/concurrency/Exception.h>
#include <thrift/lib/cpp/concurrency/FunctionRunner.h>

using namespace std;
using namespace apache::thrift;
using namespace folly;
using namespace apache::thrift::concurrency;
using namespace apache::thrift::krb5;
using apache::thrift::concurrency::FunctionRunner;
using apache::thrift::concurrency::TooManyPendingTasksException;

DEFINE_int32(
    sasl_handshake_client_num_cleanup_threads,
    1,
    "Number of background threads that clean up SASL handshake client memory");

namespace {

/**
 * This class handles cleaning up SASL handshake client contexts in a
 * background thread, so we don't do the work on an I/O thread. It just
 * wraps a ThreadManager. We access it through a folly::Singleton<>.
 * If a handshake client is destructed during program shutdown after the
 * singleton is gone, it will do the cleanup inline on the I/O thread.
 */
class KerberosSASLHandshakeClientCleanupManager {
 public:
  KerberosSASLHandshakeClientCleanupManager() {
    threadManager_ = concurrency::ThreadManager::newSimpleThreadManager(
        FLAGS_sasl_handshake_client_num_cleanup_threads);
    auto threadFactory = std::make_shared<concurrency::PosixThreadFactory>(
        concurrency::PosixThreadFactory::kDefaultPolicy,
        concurrency::PosixThreadFactory::kDefaultPriority,
        2 /* stackSizeMb */);
    threadManager_->threadFactory(threadFactory);
    threadManager_->setNamePrefix("sasl-client-cleanup-thread");
    threadManager_->start();
  }

  ~KerberosSASLHandshakeClientCleanupManager() {
    threadManager_->join();
  }

  std::shared_ptr<concurrency::ThreadManager> getThreadManager() {
    return threadManager_;
  }

 private:
  std::shared_ptr<concurrency::ThreadManager> threadManager_;
};

// singleton instance
folly::Singleton<KerberosSASLHandshakeClientCleanupManager> theCleanupManager;

}

/**
 * Client functions.
 */
KerberosSASLHandshakeClient::KerberosSASLHandshakeClient(
    const std::shared_ptr<SecurityLogger>& logger) :
    phase_(INIT),
    logger_(logger),
    securityMech_(SecurityMech::KRB5_SASL) {

  // Set required security properties, we can define setters for these if
  // they need to be modified later.
  requiredFlags_ =
    GSS_C_MUTUAL_FLAG |
    GSS_C_REPLAY_FLAG |
    GSS_C_SEQUENCE_FLAG |
    GSS_C_INTEG_FLAG |
    GSS_C_CONF_FLAG;

  context_ = GSS_C_NO_CONTEXT;
  targetName_ = GSS_C_NO_NAME;
  clientCreds_ = GSS_C_NO_CREDENTIAL;
  contextStatus_ = GSS_S_NO_CONTEXT;

  // Bitmask specifying a requirement for all security layers and max
  // buffer length from the protocol. If we ever allow different security layer
  // properties, this would need to become more dynamic.
  // Confidentiality=04, Integrity=02, None=01.
  // Select only one of them (server can support several, client chooses one)
  securityLayerBitmask_ = 0x04ffffff;
  securityLayerBitmaskBuffer_ = IOBuf::create(sizeof(securityLayerBitmask_));
  io::Appender b(securityLayerBitmaskBuffer_.get(), 0);
  b.writeBE(securityLayerBitmask_);
}

KerberosSASLHandshakeClient::~KerberosSASLHandshakeClient() {
  // Copy locally since 'this' may not exist when the async function runs
  gss_ctx_id_t context = context_;
  gss_name_t target_name = targetName_;
  gss_cred_id_t client_creds = clientCreds_;
  // Check if we actually need to clean up.
  if (context == GSS_C_NO_CONTEXT &&
      target_name == GSS_C_NO_NAME &&
      client_creds == GSS_C_NO_CREDENTIAL) {
    return;
  }
  auto logger = logger_;
  auto cleanupManager =
    folly::Singleton<KerberosSASLHandshakeClientCleanupManager>::try_get();
  if (cleanupManager == nullptr) {
    logger->log("sasl_handshake_client_sync_cleanup");
    cleanUpState(context, target_name, client_creds, logger);
    return;
  }

  auto functionRunner = std::make_shared<FunctionRunner>([=] {
    cleanUpState(context, target_name, client_creds, logger);
  });
  if (!cleanupManager->getThreadManager()->tryAdd(functionRunner)) {
    // If we can't do this async, do it inline. We don't want to leak memory.
    logger->log("sasl_handshake_client_sync_cleanup");
    cleanUpState(context, target_name, client_creds, logger);
  }
}

void KerberosSASLHandshakeClient::setSecurityMech(const SecurityMech mech) {
  securityMech_ = mech;
  if (mech == SecurityMech::KRB5_GSS_NO_MUTUAL) {
    requiredFlags_ &= ~GSS_C_MUTUAL_FLAG;
  } else {
    requiredFlags_ |= GSS_C_MUTUAL_FLAG;
  }
}

void KerberosSASLHandshakeClient::cleanUpState(
    gss_ctx_id_t context,
    gss_name_t target_name,
    gss_cred_id_t client_creds,
    const std::shared_ptr<SecurityLogger>& logger) {
  logger->logStart("clean_up_state");
  OM_uint32 min_stat;
  if (context != GSS_C_NO_CONTEXT) {
    gss_delete_sec_context(&min_stat, &context, GSS_C_NO_BUFFER);
  }
  if (target_name != GSS_C_NO_NAME) {
    gss_release_name(&min_stat, &target_name);
  }
  if (client_creds != GSS_C_NO_CREDENTIAL) {
    gss_release_cred(&min_stat, &client_creds);
  }
  logger->logEnd("clean_up_state");
}

void KerberosSASLHandshakeClient::throwKrb5Exception(
    const std::string& custom,
    krb5_context ctx,
    krb5_error_code code) {
  const char* err = krb5_get_error_message(ctx, code);
  auto msg = folly::to<std::string>(custom, ' ', err);
  krb5_free_error_message(ctx, err);
  throw TKerberosException(msg);
}

void KerberosSASLHandshakeClient::startClientHandshake() {
  assert(phase_ == INIT);

  OM_uint32 maj_stat, min_stat;
  context_ = GSS_C_NO_CONTEXT;

  string service, addr, ip;
  if (getRequiredServicePrincipal_) {
    tie(service, addr, ip) = (getRequiredServicePrincipal_)();
  } else {
    size_t at = servicePrincipal_.find("@");
    if (at == string::npos) {
      throw TKerberosException(
        "Service principal invalid: " + servicePrincipal_);
    }

    addr = servicePrincipal_.substr(at + 1);
    service = servicePrincipal_.substr(0, at);
  }

  // Make sure <addr> is non-empty
  // An empty <addr> part in the principal may trigger a large buffer
  // overflow and segfault in the glibc codebase. :(
  if (addr.empty()) {
    throw TKerberosException(
      "Service principal invalid: " + service + "@" + addr);
  }

  logger_->logStart("import_sname");
  Krb5Context ctx(true);
  auto princ = Krb5Principal::snameToPrincipal(
    ctx.get(),
    KRB5_NT_UNKNOWN,
    addr,
    service);
  string princ_name = folly::to<string>(princ);

  if (princ.getRealm().empty()) {
    throw TKerberosException(
      "Service principal invalid (empty realm). "
      "princ_name=" + princ_name + " addr=" + addr + " ip=" + ip);
  }

  gss_buffer_desc service_name_token;
  service_name_token.value = (void *)princ_name.c_str();
  service_name_token.length = princ_name.size() + 1;

  maj_stat = gss_import_name(
    &min_stat,
    &service_name_token,
    (gss_OID) gss_nt_krb5_name,
    &targetName_);
  if (maj_stat != GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error parsing server name on client", maj_stat, min_stat);
  }
  logger_->logEnd("import_sname");

  unique_ptr<gss_name_t, GSSNameDeleter> client_name(new gss_name_t);
  *client_name = GSS_C_NO_NAME;

  if (clientPrincipal_.size() > 0) {
    logger_->logStart("import_cname");
    // If a client principal was explicitly specified, then establish
    // credentials using that principal, otherwise use the default.
    gss_buffer_desc client_name_tok;
    // It's ok to grab a c_str() pointer here since client_name_tok only
    // needs to be valid for a couple lines, in which the clientPrincipal_
    // is not modified.
    client_name_tok.value = (void *)clientPrincipal_.c_str();
    client_name_tok.length = clientPrincipal_.size() + 1;

    maj_stat = gss_import_name(
      &min_stat,
      &client_name_tok,
      (gss_OID) gss_nt_krb5_name,
      client_name.get());
    if (maj_stat != GSS_S_COMPLETE) {
      KerberosSASLHandshakeUtils::throwGSSException(
        "Error parsing client name on client", maj_stat, min_stat);
    }
    logger_->logEnd("import_cname");
  }

  // Attempt to acquire client credentials.
  if (!credentialsCacheManager_) {
    throw TKerberosException("Credentials cache manager not provided");
  }

  try {
    logger_->logStart("wait_for_cache");
    cc_ = credentialsCacheManager_->waitForCache(princ, logger_.get());
    logger_->logEnd("wait_for_cache");
  } catch (const std::runtime_error& e) {
    throw TKerberosException(
      string("Kerberos ccache init error: ") + e.what());
  }

  logger_->logStart("import_cred");
  maj_stat = gss_krb5_import_cred(
    &min_stat,
    cc_->get(),
    nullptr,
    nullptr,
    &clientCreds_);
  logger_->logEnd("import_cred");

  if (maj_stat != GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error establishing client credentials", maj_stat, min_stat);
  }

  // Init phase complete, start establishing security context
  phase_ = ESTABLISH_CONTEXT;
  initSecurityContext();
}

void KerberosSASLHandshakeClient::initSecurityContext() {
  assert(phase_ == ESTABLISH_CONTEXT);

  OM_uint32 ret_flags;
  OM_uint32 maj_stat, min_stat;

  outputToken_.reset(new gss_buffer_desc);
  *outputToken_ = GSS_C_EMPTY_BUFFER;

  bool first_call = false;
  if (context_ == GSS_C_NO_CONTEXT) {
    first_call = true;
    logger_->logStart("init_sec_context");
  } else {
    logger_->logStart("cont_init_sec_context");
  }

  OM_uint32 time_rec = 0;
  contextStatus_ = gss_init_sec_context(
    &min_stat, // minor status
    clientCreds_,
    &context_, // context
    targetName_, // what we're connecting to
    (gss_OID) gss_mech_krb5, // mech type, default to krb 5
    requiredFlags_, // flags
    GSS_C_INDEFINITE, // Max lifetime, will be controlled by connection
                      // lifetime. Limited by lifetime indicated in
                      // krb5.conf file.
    nullptr, // channel bindings
    inputToken_.get() != nullptr ? inputToken_.get() : GSS_C_NO_BUFFER,
    nullptr, // mech type
    outputToken_.get(), // output token
    &retFlags_, // return flags
    &time_rec // time_rec
  );

  if (first_call) {
    logger_->logEnd("init_sec_context");
  } else {
    logger_->logEnd("cont_init_sec_context");
  }

  if (contextStatus_ != GSS_S_COMPLETE &&
      contextStatus_ != GSS_S_CONTINUE_NEEDED) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error initiating client context",
      contextStatus_,
      min_stat);
  }

  if (contextStatus_ == GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::getContextData(
      context_,
      contextLifetime_,
      contextSecurityFlags_,
      establishedClientPrincipal_,
      establishedServicePrincipal_);

    if ((requiredFlags_ & contextSecurityFlags_) != requiredFlags_) {
      throw TKerberosException("Not all security properties established");
    }

    phase_ = (securityMech_ == SecurityMech::KRB5_GSS_NO_MUTUAL ||
              securityMech_ == SecurityMech::KRB5_GSS)
             ? COMPLETE : CONTEXT_NEGOTIATION_COMPLETE;
  }
}

std::unique_ptr<std::string> KerberosSASLHandshakeClient::getTokenToSend() {
  switch(phase_) {
    case INIT:
      // Should not call this function if in INIT state
      assert(false);
    case ESTABLISH_CONTEXT:
    case CONTEXT_NEGOTIATION_COMPLETE:
    case COMPLETE:
    {
      if (phase_ == COMPLETE &&
          securityMech_ != SecurityMech::KRB5_GSS_NO_MUTUAL) {
        // Complete state should only have a token to send if we're not doing
        // mutual auth.
        break;
      }
      if (phase_ == ESTABLISH_CONTEXT) {
        logger_->logEnd("prepare_first_request");
      } else if (phase_ == CONTEXT_NEGOTIATION_COMPLETE) {
        logger_->logEnd("prepare_second_request");
      }
      return unique_ptr<string>(
        new string((const char*) outputToken_->value, outputToken_->length));
      break;
    }
    case SELECT_SECURITY_LAYER:
    {
      unique_ptr<IOBuf> wrapped_sec_layer_message = wrapMessage(
        IOBuf::copyBuffer(
          securityLayerBitmaskBuffer_->data(),
          securityLayerBitmaskBuffer_->length()));
      wrapped_sec_layer_message->coalesce();
      auto ptr = unique_ptr<string>(new string(
        (char *)wrapped_sec_layer_message->data(),
        wrapped_sec_layer_message->length()
      ));
      logger_->logEnd("prepare_third_request");
      return ptr;
      break;
    }
    default:
      break;
  }
  return nullptr;
}

void KerberosSASLHandshakeClient::handleResponse(const string& msg) {
  switch(phase_) {
    case INIT:
      // Should not call this function if in INIT state
      assert(false);
    case ESTABLISH_CONTEXT:
      if (msg.length() == 0) {
        throw TKerberosException("Security negotiation failed, empty response");
      }
      logger_->logEnd("first_rtt");
      logger_->logStart("prepare_second_request");
      assert(contextStatus_ == GSS_S_CONTINUE_NEEDED);
      if (inputToken_ == nullptr) {
        inputToken_.reset(new gss_buffer_desc);
      }
      inputToken_->length = msg.length();
      inputTokenValue_ = vector<unsigned char>(msg.begin(), msg.end());
      inputToken_->value = &inputTokenValue_[0];
      initSecurityContext();
      break;
    case CONTEXT_NEGOTIATION_COMPLETE:
    {
      logger_->logEnd("second_rtt");
      logger_->logStart("prepare_third_request");
      unique_ptr<IOBuf> unwrapped_security_layer_msg = unwrapMessage(
        IOBuf::copyBuffer(msg));
      io::Cursor c = io::Cursor(unwrapped_security_layer_msg.get());
      uint32_t security_layers = c.readBE<uint32_t>();
      if ((security_layers & securityLayerBitmask_) >> 24 == 0 ||
          (security_layers & 0x00ffffff) != 0x00ffffff) {
        // the top 8 bits contain:
        // in security_layers (received from server):
        //    a bitmask of the available layers
        // in securityLayerBitmask_ (local):
        //    selected layer
        // bottom 3 bytes contain the max buffer size
        throw TKerberosException("Security layer negotiation failed");
      }
      phase_ = SELECT_SECURITY_LAYER;
      break;
    }
    case SELECT_SECURITY_LAYER:
      logger_->logEnd("third_rtt");
      // If we are in select security layer state and we get any message
      // from the server, it means that the server is successful, so complete
      // the handshake
      phase_ = COMPLETE;
      break;
    default:
      break;
  }
}

bool KerberosSASLHandshakeClient::isContextEstablished() {
  return phase_ == COMPLETE;
}

PhaseType KerberosSASLHandshakeClient::getPhase() {
  return phase_;
}

void KerberosSASLHandshakeClient::setRequiredServicePrincipal(
  const std::string& service) {

  assert(phase_ == INIT);
  servicePrincipal_ = service;
}

void KerberosSASLHandshakeClient::setRequiredClientPrincipal(
  const std::string& client) {

  assert(phase_ == INIT);
  clientPrincipal_ = client;
}

void KerberosSASLHandshakeClient::setRequiredServicePrincipalFetcher(
  std::function<std::tuple<std::string, std::string, std::string>()>&&
    function) {

  assert(phase_ == INIT);
  getRequiredServicePrincipal_ = std::move(function);
}

const string& KerberosSASLHandshakeClient::getEstablishedServicePrincipal()
  const {

  assert(phase_ == COMPLETE);
  return establishedServicePrincipal_;
}

const string& KerberosSASLHandshakeClient::getEstablishedClientPrincipal()
  const {

  assert(phase_ == COMPLETE);
  return establishedClientPrincipal_;
}

unique_ptr<folly::IOBuf> KerberosSASLHandshakeClient::wrapMessage(
    unique_ptr<folly::IOBuf>&& buf) {
  assert(contextStatus_ == GSS_S_COMPLETE);
  return KerberosSASLHandshakeUtils::wrapMessage(
    context_,
    std::move(buf)
  );
}

unique_ptr<folly::IOBuf> KerberosSASLHandshakeClient::unwrapMessage(
    unique_ptr<folly::IOBuf>&& buf) {
  assert(contextStatus_ == GSS_S_COMPLETE);
  return KerberosSASLHandshakeUtils::unwrapMessage(
    context_,
    std::move(buf)
  );
}
