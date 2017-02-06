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

#ifndef THRIFT_SASLCLIENT_H_
#define THRIFT_SASLCLIENT_H_ 1

#include <folly/io/async/EventBase.h>
#include <thrift/lib/cpp/transport/TTransportException.h>
#include <thrift/lib/cpp2/async/SaslEndpoint.h>
#include <thrift/lib/cpp2/security/SecurityLogger.h>
#include <folly/io/async/HHWheelTimer.h>
#include <thrift/lib/cpp2/security/KerberosSASLThreadManager.h>
#include <thrift/lib/cpp2/security/KerberosSASLHandshakeUtils.h>
#include <thrift/lib/cpp/util/kerberos/Krb5CredentialsCacheManager.h>

#include <folly/ExceptionWrapper.h>

namespace apache { namespace thrift {

class SaslClient : public SaslEndpoint {
 public:
  class Callback : public folly::HHWheelTimer::Callback {
   public:
    ~Callback() override {}

    // This will be called just before the kerberos operation starts.
    // This allows the caller to implement more effective timeouts.
    virtual void saslStarted() = 0;

    // Invoked when a new message should be sent to the server.
    virtual void saslSendServer(std::unique_ptr<folly::IOBuf>&&) = 0;

    // Invoked when the most recently consumed message results in an
    // error.  Continuation is impossible at this point.
    virtual void saslError(folly::exception_wrapper&&) = 0;

    // Invoked when the most recently consumed message completes the
    // SASL exchange successfully.
    virtual void saslComplete() = 0;

    void timeoutExpired() noexcept override {
      using apache::thrift::transport::TTransportException;
      saslError(folly::make_exception_wrapper<TTransportException>(
          TTransportException::SASL_HANDSHAKE_TIMEOUT,
          "SASL handshake timed out"));
    }
  };

  explicit SaslClient(
      folly::EventBase* evb = nullptr,
      const std::shared_ptr<SecurityLogger>& logger = nullptr)
    : SaslEndpoint(evb)
    , saslLogger_(logger)
    , securityMech_(std::make_shared<SecurityMech>(SecurityMech::KRB5_GSS)) {}

  virtual void setClientIdentity(const std::string& identity) = 0;
  virtual void setServiceIdentity(const std::string& identity) = 0;
  virtual void setRequiredServicePrincipalFetcher(
    std::function<std::tuple<std::string, std::string, std::string>()>
      /*function*/) {}
  virtual void setSecurityMech(const SecurityMech& mech) {
    securityMech_ = std::make_shared<SecurityMech>(mech);
  }
  virtual SecurityMech getSecurityMech() const {
    return *securityMech_;
  }

  // This will create the initial message, and pass it to
  // cb->saslSendServer().  If there is an error, cb->saslError() will
  // be invoked.
  virtual void start(Callback*) = 0;

  // This will consume the provided message.  If a message should be
  // sent in reply, it will be passed to cb->saslServerMessage().  If
  // the authentication completes successfully, cb->saslComplete()
  // will be invoked.  If there is an error, cb->saslError() will be
  // invoked.
  virtual void consumeFromServer(
    Callback*, std::unique_ptr<folly::IOBuf>&&) = 0;

  virtual const std::string* getErrorString() const = 0;

  virtual void setErrorString(const std::string& str) = 0;

  virtual void setSaslThreadManager(
    const std::shared_ptr<SaslThreadManager>& /*thread_manager*/) {};

  virtual void setCredentialsCacheManager(
    const std::shared_ptr<krb5::Krb5CredentialsCacheManager>& /*cc_manager*/) {}

  std::shared_ptr<SecurityLogger> getSaslLogger() {
    return saslLogger_;
  }

 protected:
  std::shared_ptr<SecurityLogger> saslLogger_;
  std::shared_ptr<SecurityMech> securityMech_;
};

}} // apache::thrift

#endif // THRIFT_SASLCLIENT_H_
