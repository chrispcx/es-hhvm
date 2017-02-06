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

#include <thrift/lib/cpp2/async/GssSaslClient.h>

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <folly/Memory.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GFlags.h>

#include <thrift/lib/cpp/concurrency/FunctionRunner.h>
#include <thrift/lib/cpp2/protocol/MessageSerializer.h>
#include <thrift/lib/cpp2/gen-cpp2/Sasl_types.h>
#include <thrift/lib/cpp2/gen-cpp2/SaslAuthService.tcc>
#include <thrift/lib/cpp2/security/KerberosSASLHandshakeClient.h>
#include <thrift/lib/cpp2/security/KerberosSASLHandshakeUtils.h>
#include <thrift/lib/cpp2/security/KerberosSASLThreadManager.h>
#include <thrift/lib/cpp2/security/SecurityLogger.h>
#include <thrift/lib/cpp/concurrency/Exception.h>
#include <thrift/lib/cpp/concurrency/PosixThreadFactory.h>

#include <chrono>
#include <memory>

using folly::IOBuf;
using folly::IOBufQueue;
using apache::thrift::concurrency::Guard;
using apache::thrift::concurrency::Mutex;
using apache::thrift::concurrency::FunctionRunner;
using apache::thrift::concurrency::PosixThreadFactory;
using apache::thrift::concurrency::ThreadManager;
using apache::thrift::concurrency::TooManyPendingTasksException;
using apache::thrift::transport::TTransportException;

using namespace std;
using apache::thrift::sasl::SaslStart;
using apache::thrift::sasl::SaslRequest;
using apache::thrift::sasl::SaslReply;
using apache::thrift::sasl::SaslAuthService_authFirstRequest_pargs;
using apache::thrift::sasl::SaslAuthService_authFirstRequest_presult;
using apache::thrift::sasl::SaslAuthService_authNextRequest_pargs;
using apache::thrift::sasl::SaslAuthService_authNextRequest_presult;

DEFINE_int32(sasl_thread_manager_timeout_ms, 1000,
  "Max number of ms for sasl tasks to wait in the thread manager queue");

namespace apache { namespace thrift {

static const char KRB5_SASL[] = "krb5";
static const char KRB5_GSS[] = "gss";
static const char KRB5_GSS_NO_MUTUAL[] = "gssnm";

GssSaslClient::GssSaslClient(folly::EventBase* evb,
      const std::shared_ptr<SecurityLogger>& logger)
    : SaslClient(evb, logger)
    , clientHandshake_(new KerberosSASLHandshakeClient(logger))
    , mutex_(new Mutex)
    , saslThreadManager_(nullptr)
    , seqId_(new int(0))
    , protocol_(0xFFFF)
    , inProgress_(std::make_shared<bool>(false)) {
}

std::chrono::milliseconds GssSaslClient::getCurTime() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch());
}

void GssSaslClient::start(Callback *cb) {

  auto evb = evb_;
  auto clientHandshake = clientHandshake_;
  auto mutex = mutex_;
  auto logger = saslLogger_;
  auto proto = protocol_;
  auto seqId = seqId_;
  auto threadManager = saslThreadManager_;
  auto inProgress = inProgress_;
  auto threadManagerTimeout = FLAGS_sasl_thread_manager_timeout_ms;
  auto securityMech = securityMech_;
  logger->logValue("security_mech", (int64_t)*securityMech);

  logger->logStart("prepare_first_request");

  folly::exception_wrapper ew_tm;
  if (!threadManager) {
    ew_tm = folly::make_exception_wrapper<TApplicationException>(
      "saslThreadManager is not set in GssSaslClient");
  } else if (!threadManager->isHealthy()) {
    ew_tm = folly::make_exception_wrapper<TKerberosException>(
      "SASL thread pool is not healthy.");
  } else {
    ew_tm = folly::try_and_catch<
        TooManyPendingTasksException, TKerberosException>([&]() {
      logger->logStart("thread_manager_overhead");
      uint64_t before = getCurTime().count();
      *inProgress = true;
      threadManager->start(std::make_shared<FunctionRunner>([=] {
        logger->logEnd("thread_manager_overhead");
        uint64_t after = getCurTime().count();

        unique_ptr<IOBuf> iobuf;
        folly::exception_wrapper ex;

        threadManager->recordActivity();

        bool isHealthy = threadManager->isHealthy();
        bool tmTimeout = (after - before) > threadManagerTimeout;

        if (isHealthy && !tmTimeout) {
          Guard guard(*mutex);
          if (!*evb) {
            return;
          }

          (*evb)->runInEventBaseThread([=] () mutable {
              if (!*evb) {
                return;
              }
              cb->saslStarted();
            });
        }

        if (!isHealthy) {
          ex = folly::make_exception_wrapper<TKerberosException>(
            "Draining SASL thread pool");
        } else if (tmTimeout) {
          ex = folly::make_exception_wrapper<TKerberosException>(
            "Timed out due to thread manager lag");
        } else {
          ex = folly::try_and_catch<std::exception, TTransportException,
              TProtocolException, TApplicationException,
              TKerberosException>([&]() {
            clientHandshake->setSecurityMech(*securityMech);
            clientHandshake->startClientHandshake();
            auto token = clientHandshake->getTokenToSend();

            SaslStart start;
            start.mechanism = KRB5_SASL;
            // Prefer GSS mech
            if (*securityMech == SecurityMech::KRB5_GSS) {
              start.__isset.mechanisms = true;
              start.mechanisms.push_back(KRB5_GSS);
            } else if (*securityMech == SecurityMech::KRB5_GSS_NO_MUTUAL) {
              start.mechanism = KRB5_GSS_NO_MUTUAL;
            }
            if (token != nullptr) {
              start.request.response = *token;
              start.request.__isset.response = true;
            }
            start.__isset.request = true;
            SaslAuthService_authFirstRequest_pargs argsp;
            argsp.get<0>().value = &start;

            iobuf = PargsPresultProtoSerialize(
              proto, argsp, "authFirstRequest", T_CALL, (*seqId)++);
          });
        }

        Guard guard(*mutex);
        // Return if channel is unavailable. Ie. evb_ may not be good.
        if (!*evb) {
          return;
        }

        // Log the overhead around rescheduling the remainder of the
        // handshake at the back of the evb queue.
        logger->logStart("evb_overhead");
        (*evb)->runInEventBaseThread([ =, iobuf = std::move(iobuf) ]() mutable {
          logger->logEnd("evb_overhead");
          if (!*evb) {
            return;
          }
          if (ex) {
            cb->saslError(std::move(ex));
            if (*inProgress) {
              threadManager->end();
              *inProgress = false;
            }
            return;
          } else {
            logger->logStart("first_rtt");
            cb->saslSendServer(std::move(iobuf));
            // If the context was already established, we're free to send
            // the actual request.
            if (clientHandshake_->isContextEstablished()) {
              cb->saslComplete();
              if (*inProgress) {
                threadManager->end();
                *inProgress = false;
              }
            }
          }
        });
      }));
    });
  }
  if (ew_tm) {
    if (ew_tm.is_compatible_with<TooManyPendingTasksException>()) {
      logger->log("too_many_pending_tasks_in_start");
    } else if (ew_tm.is_compatible_with<TKerberosException>()){
      logger->log("sasl_thread_pool_unhealthy");
    }
    // Since we haven't started, we need to make sure we unset the
    // inProgress indicator.
    *inProgress = false;
    cb->saslError(std::move(ew_tm));
    // no end() here.  If this happens, we never really started.
  }
}

void GssSaslClient::consumeFromServer(
  Callback *cb, std::unique_ptr<IOBuf>&& message) {
  std::shared_ptr<IOBuf> smessage(std::move(message));

  auto evb = evb_;
  auto clientHandshake = clientHandshake_;
  auto mutex = mutex_;
  auto logger = saslLogger_;
  auto proto = protocol_;
  auto seqId = seqId_;
  auto threadManager = saslThreadManager_;
  auto inProgress = inProgress_;
  auto threadManagerTimeout = FLAGS_sasl_thread_manager_timeout_ms;
  auto securityMech = securityMech_;

  folly::exception_wrapper ew_tm;
  if (!threadManager->isHealthy()) {
    ew_tm = folly::make_exception_wrapper<TKerberosException>(
      "SASL thread pool is not healthy.");
  } else {
    ew_tm = folly::try_and_catch<
        TooManyPendingTasksException, TKerberosException>([&]() {
      uint64_t before = getCurTime().count();
      threadManager->get()->add(std::make_shared<FunctionRunner>([=] {
        uint64_t after = getCurTime().count();
        std::string req_data;
        unique_ptr<IOBuf> iobuf;
        folly::exception_wrapper ex;

        threadManager->recordActivity();

        bool isHealthy = threadManager->isHealthy();
        bool tmTimeout = (after - before) > threadManagerTimeout;
        if (isHealthy && !tmTimeout) {
          Guard guard(*mutex);
          if (!*evb) {
            return;
          }

          (*evb)->runInEventBaseThread([=] () mutable {
              if (!*evb) {
                return;
              }
              cb->saslStarted();
            });
        }

        // Get the input string or outcome status
        std::string input = "";
        bool finished = false;
        // SaslAuthService_authFirstRequest_presult should be structurally
        // identical to SaslAuthService_authNextRequest_presult
        static_assert(sizeof(SaslAuthService_authFirstRequest_presult) ==
                      sizeof(SaslAuthService_authNextRequest_presult),
                      "Types should be structurally identical");
        static_assert(std::is_same<
            SaslAuthService_authFirstRequest_presult,
            SaslAuthService_authNextRequest_presult>::value,
          "Types should be structurally identical");

        if (!isHealthy) {
          ex = folly::make_exception_wrapper<TKerberosException>(
            "Draining SASL thread pool");
        } else if (tmTimeout) {
          ex = folly::make_exception_wrapper<TKerberosException>(
            "Timed out due to thread manager lag");
        } else {
          ex = folly::try_and_catch<std::exception, TTransportException,
              TProtocolException, TApplicationException,
              TKerberosException>([&]() {
            SaslReply reply;
            SaslAuthService_authFirstRequest_presult presult;
            presult.get<0>().value = &reply;
            string methodName;
            try {
              methodName = PargsPresultProtoDeserialize(
                  proto, presult, smessage.get(), T_REPLY).first;
            } catch (const TProtocolException& e) {
              if (proto == protocol::T_BINARY_PROTOCOL &&
                  e.getType() == TProtocolException::BAD_VERSION) {
                // We used to use compact always in security messages,
                // even when the header said they should be binary. If we
                // end up in this if, we're talking to an old version
                // remote end, so try compact too.
                methodName = PargsPresultProtoDeserialize(
                    protocol::T_COMPACT_PROTOCOL,
                    presult,
                    smessage.get(),
                    T_REPLY).first;
              } else {
                throw;
              }
            }
            if (methodName != "authFirstRequest" &&
                methodName != "authNextRequest") {
              throw TApplicationException(
                "Bad return method name: " + methodName);
            }
            if (reply.__isset.challenge) {
              input = reply.challenge;
            }
            if (reply.__isset.outcome) {
              finished = reply.outcome.success;
            }

            // If the mechanism is gss, then set the clientHandshake class to
            // only do gss.
            if (reply.__isset.mechanism && reply.mechanism == KRB5_GSS) {
              *securityMech = SecurityMech::KRB5_GSS;
            } else if (reply.__isset.mechanism &&
                       reply.mechanism == KRB5_GSS_NO_MUTUAL) {
              throw TKerberosException(
                "Should never get a reply from a server with NO_MUTUAL mech");
            } else {
              *securityMech = SecurityMech::KRB5_SASL;
            }
            clientHandshake->setSecurityMech(*securityMech);

            logger->logValue("security_mech", (int64_t)*securityMech);

            clientHandshake->handleResponse(input);
            auto token = clientHandshake->getTokenToSend();
            if (clientHandshake->getPhase() == COMPLETE) {
              assert(token == nullptr);
              if (finished != true) {
                throw TKerberosException(
                  "Outcome of false returned from server");
              }
            }
            if (token != nullptr) {
              SaslRequest req;
              req.response = *token;
              req.__isset.response = true;
              SaslAuthService_authNextRequest_pargs argsp;
              argsp.get<0>().value = &req;
              iobuf = PargsPresultProtoSerialize(
                proto, argsp, "authNextRequest", T_CALL, (*seqId)++);
            }
          });
        }

        Guard guard(*mutex);
        // Return if channel is unavailable. Ie. evb_ may not be good.
        if (!*evb) {
          return;
        }

        auto phase = clientHandshake->getPhase();
        (*evb)->runInEventBaseThread([ =, iobuf = std::move(iobuf) ]() mutable {
          if (!*evb) {
            return;
          }
          if (ex) {
            cb->saslError(std::move(ex));
            if (*inProgress) {
              threadManager->end();
              *inProgress = false;
            }
            return;
          }
          if (iobuf && !iobuf->empty()) {
            if (phase == SELECT_SECURITY_LAYER) {
              logger->logStart("third_rtt");
            } else {
              logger->logStart("second_rtt");
            }
            cb->saslSendServer(std::move(iobuf));
          }
          if (clientHandshake_->isContextEstablished()) {
            cb->saslComplete();
            if (*inProgress) {
              threadManager->end();
              *inProgress = false;
            }
          }
        });
      }));
    });
  }
  if (ew_tm) {
    if (ew_tm.is_compatible_with<TooManyPendingTasksException>()) {
      logger->log("too_many_pending_tasks_in_consume");
    } else if (ew_tm.is_compatible_with<TKerberosException>()){
      logger->log("sasl_thread_pool_unhealthy");
    }
    cb->saslError(std::move(ew_tm));
    if (*inProgress) {
      threadManager->end();
      *inProgress = false;
    }
  }
}

std::unique_ptr<IOBuf> GssSaslClient::encrypt(
    std::unique_ptr<IOBuf>&& buf) {
  return clientHandshake_->wrapMessage(std::move(buf));
}

std::unique_ptr<IOBuf> GssSaslClient::decrypt(
    std::unique_ptr<IOBuf>&& buf) {
  return clientHandshake_->unwrapMessage(std::move(buf));
}

std::string GssSaslClient::getClientIdentity() const {
  if (clientHandshake_->isContextEstablished()) {
    return clientHandshake_->getEstablishedClientPrincipal();
  } else {
    return "";
  }
}

std::string GssSaslClient::getServerIdentity() const {
  if (clientHandshake_->isContextEstablished()) {
    return clientHandshake_->getEstablishedServicePrincipal();
  } else {
    return "";
  }
}

void GssSaslClient::detachEventBase() {
  apache::thrift::concurrency::Guard guard(*mutex_);
  if (*inProgress_) {
    saslThreadManager_->end();
    *inProgress_ = false;
  }
  *evb_ = nullptr;
}

void GssSaslClient::attachEventBase(
    folly::EventBase* evb) {
  apache::thrift::concurrency::Guard guard(*mutex_);
  *evb_ = evb;
}

}}
