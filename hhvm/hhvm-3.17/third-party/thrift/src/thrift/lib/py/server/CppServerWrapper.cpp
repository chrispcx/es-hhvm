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

#include <map>

#include <glog/logging.h>

#include <boost/python/class.hpp>
#include <boost/python/def.hpp>
#include <boost/python/dict.hpp>
#include <boost/python/enum.hpp>
#include <boost/python/errors.hpp>
#include <boost/python/import.hpp>
#include <boost/python/list.hpp>
#include <boost/python/module.hpp>
#include <boost/python/tuple.hpp>

#include <thrift/lib/cpp/concurrency/PosixThreadFactory.h>
#include <thrift/lib/cpp/concurrency/ThreadManager.h>
#include <thrift/lib/cpp2/async/AsyncProcessor.h>
#include <thrift/lib/cpp2/security/TLSCredProcessor.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>
#include <thrift/lib/cpp/protocol/TProtocolTypes.h>
#include <thrift/lib/cpp2/protocol/BinaryProtocol.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>
#include <thrift/lib/py/server/CppContextData.h>
#include <folly/Memory.h>
#include <folly/ScopeGuard.h>
#include <wangle/ssl/SSLContextConfig.h>

using namespace apache::thrift;
using apache::thrift::concurrency::PosixThreadFactory;
using apache::thrift::concurrency::ThreadManager;
using apache::thrift::transport::THeader;
using apache::thrift::server::TServerEventHandler;
using apache::thrift::server::TServerObserver;
using apache::thrift::server::TConnectionContext;
using folly::SSLContext;
using wangle::SSLContextConfig;
using wangle::SSLCacheOptions;
using namespace boost::python;

namespace {

object makePythonHeaders(const std::map<std::string, std::string>& cppheaders) {
  object headers = dict();
  for (const auto& it : cppheaders) {
    headers[it.first] = it.second;
  }
  return headers;
}

object makePythonList(const std::vector<std::string>& vec) {
  list result;
  for (auto it = vec.begin(); it != vec.end(); ++it) {
    result.append(*it);
  }
  return result;
}

std::string getStringAttrSafe(object& pyObject, const char* attrName) {
  object val = pyObject.attr(attrName);
  if (val.is_none()) {
    return "";
  }
  return extract<std::string>(str(val));
}

template<class T>
T getIntAttr(object& pyObject, const char* attrName) {
  object val = pyObject.attr(attrName);
  return extract<T>(val);
}

std::list<std::string> getStringListSafe(object& pyObject, const char* attr) {
  object val = pyObject.attr(attr);
  std::list<std::string> result;
  if (val.is_none()) {
    return result;
  }
  auto exList = extract<list>(val);
  if (exList.check()) {
    list pyList = exList;
    int len = boost::python::len(pyList);
    for (int i = 0; i < len; ++i) {
      result.push_back(extract<std::string>(str(pyList[i])));
    }
  }
  return result;
}

}

class CallbackWrapper {
public:
  void call(object obj) {
    callback_(obj);
  }

  void setCallback(folly::Function<void(object)>&& callback) {
    callback_ = std::move(callback);
  }

private:
  folly::Function<void(object)> callback_;
};

class CppServerEventHandler : public TServerEventHandler {
public:
  explicit CppServerEventHandler(object serverEventHandler)
    : handler_(std::make_shared<object>(serverEventHandler)) {}

  void newConnection(TConnectionContext* ctx) override {
    callPythonHandler(ctx, "newConnection");
  }

  void connectionDestroyed(TConnectionContext* ctx) override {
    callPythonHandler(ctx, "connectionDestroyed");
  }

private:
  void callPythonHandler(TConnectionContext *ctx, const char* method) {
    PyGILState_STATE state = PyGILState_Ensure();
    SCOPE_EXIT { PyGILState_Release(state); };

    // This cast always succeeds because it is called from Cpp2Connection.
    Cpp2ConnContext *cpp2Ctx = dynamic_cast<Cpp2ConnContext*>(ctx);
    auto cd_cls = handler_->attr("CONTEXT_DATA");
    object contextData = cd_cls();
    extract<CppContextData&>(contextData)().copyContextContents(cpp2Ctx);
    auto ctx_cls = handler_->attr("CPP_CONNECTION_CONTEXT");
    object cppConnContext = ctx_cls(contextData);
    handler_->attr(method)(cppConnContext);
  }

  std::shared_ptr<object> handler_;
};

class CppServerObserver : public TServerObserver {
public:
  explicit CppServerObserver(object serverObserver)
    : observer_(serverObserver) {}

  void connAccepted() override { this->call("connAccepted"); }
  void connDropped() override { this->call("connDropped"); }
  void connRejected() override { this->call("connRejected"); }
  void saslError() override { this->call("saslError"); }
  void saslFallBack() override { this->call("saslFallback"); }
  void saslComplete() override { this->call("saslComplete"); }
  void tlsError() override { this->call("tlsError"); }
  void tlsComplete() override { this->call("tlsComplete"); }
  void tlsFallback() override { this->call("tlsFallback"); }
  void tlsResumption() override { this->call("tlsResumption"); }
  void taskKilled() override { this->call("taskKilled"); }
  void taskTimeout() override { this->call("taskTimeout"); }
  void serverOverloaded() override { this->call("serverOverloaded"); }
  void receivedRequest() override { this->call("receivedRequest"); }
  void queuedRequests(int32_t n) override { this->call("queudRequests", n); }
  void queueTimeout() override { this->call("queueTimeout"); }
  void sentReply() override { this->call("sentReply"); }
  void activeRequests(int32_t n) override { this->call("activeRequests", n); }
  void callCompleted(const CallTimestamps& runtimes) override {
     this->call("callCompleted", runtimes);
   }

private:
  template<class ... Types>
  void call(const char* method_name, Types ... args) {
    PyGILState_STATE state = PyGILState_Ensure();
    SCOPE_EXIT { PyGILState_Release(state); };

    // check if the object has an attribute, because we want to be accepting
    // if we added a new listener callback and didn't yet update call the
    // people using this interface.
    if (!PyObject_HasAttrString(observer_.ptr(), method_name)) {
      return;
    }

    try {
      (void)observer_.attr(method_name)(args...);
    } catch (const error_already_set&) {
      // print the error to sys.stderr and carry on, because raising here
      // would break the server protocol, and raising in Python later
      // would be extremely disconnected and confusing since it would
      // happen in apparently unconnected Python code.
      PyErr_Print();
    }
  }

  object observer_;
};

class PythonAsyncProcessor : public AsyncProcessor {
public:
  explicit PythonAsyncProcessor(std::shared_ptr<object> adapter)
    : adapter_(adapter) {
    getPythonOnewayMethods();
  }

  // Create a task and add it to thread manager's queue. Essentially the same
  // as GeneratedAsyncProcessor's processInThread method.
  void process(std::unique_ptr<ResponseChannel::Request> req,
               std::unique_ptr<folly::IOBuf> buf,
               apache::thrift::protocol::PROTOCOL_TYPES protType,
               Cpp2RequestContext* context,
               folly::EventBase* eb,
               apache::thrift::concurrency::ThreadManager* tm) override {
    bool oneway = isOnewayMethod(buf.get(), context->getHeader());
    if (oneway && !req->isOneway()) {
      req->sendReply(std::unique_ptr<folly::IOBuf>());
    }
    auto preq = req.get();
    try {
      tm->add(
          std::make_shared<apache::thrift::PriorityEventTask>(
            // Task priority isn't supported in Python yet.
            apache::thrift::concurrency::NORMAL,
            [=, buf = std::move(buf)]() mutable {
              auto req_up = std::unique_ptr<ResponseChannel::Request>(preq);

              SCOPE_EXIT {
                eb->runInEventBaseThread(
                  [req_up = std::move(req_up)]() mutable {
                    req_up = {};
                  }
                );
              };

              if (!oneway && !req_up->isActive()) {
                return;
              }

              folly::ByteRange input_range = buf->coalesce();
              auto input_data = const_cast<unsigned char*>(input_range.data());
              auto clientType = context->getHeader()->getClientType();
              if (clientType == THRIFT_HEADER_SASL_CLIENT_TYPE) {
                // SASL processing is already done, and we're not going to put
                // it back.  So just use standard header here.
                clientType = THRIFT_HEADER_CLIENT_TYPE;
              }

              {
                PyGILState_STATE state = PyGILState_Ensure();
                SCOPE_EXIT { PyGILState_Release(state); };

#if PY_MAJOR_VERSION == 2
                auto input = handle<>(
                  PyBuffer_FromMemory(input_data, input_range.size()));
#else
                auto input = handle<>(
                  PyMemoryView_FromMemory(reinterpret_cast<char*>(input_data),
                                          input_range.size(), PyBUF_READ));
#endif

                auto cd_ctor = adapter_->attr("CONTEXT_DATA");
                object contextData = cd_ctor();
                extract<CppContextData&>(contextData)().copyContextContents(
                    context->getConnectionContext());

                auto cb_ctor = adapter_->attr("CALLBACK_WRAPPER");
                object callbackWrapper = cb_ctor();
                extract<CallbackWrapper&>(callbackWrapper)().setCallback(
                    [oneway, req_up = std::move(req_up), context, eb]
                    (object output) mutable {
                  // Make sure the request is deleted in evb.
                  SCOPE_EXIT {
                    eb->runInEventBaseThread([req_up = std::move(req_up)]
                    () mutable {
                      req_up = {};
                    });
                  };

                  // Always called from python so no need to grab GIL.
                  try {
                    std::unique_ptr<folly::IOBuf> outbuf;
                    if (output.is_none()) {
                      throw std::runtime_error(
                          "Unexpected error in processor method");
                    }
                    PyObject* output_ptr = output.ptr();
#if PY_MAJOR_VERSION == 2
                    if (PyString_Check(output_ptr)) {
                      int len = extract<int>(output.attr("__len__")());
                      if (len == 0) {
                        return;
                      }
                      outbuf = folly::IOBuf::copyBuffer(
                          extract<const char *>(output), len);
                    } else
#endif
                    if (PyBytes_Check(output_ptr)) {
                      int len = PyBytes_Size(output_ptr);
                      if (len == 0) {
                        return;
                      }
                      outbuf = folly::IOBuf::copyBuffer(
                          PyBytes_AsString(output_ptr), len);
                    } else {
                      throw std::runtime_error("Return from processor "
                          "method is not string or bytes");
                    }

                    if (!req_up->isActive()) {
                      return;
                    }
                    auto q = THeader::transform(
                          std::move(outbuf),
                          context->getHeader()->getWriteTransforms(),
                          context->getHeader()->getMinCompressBytes());
                    eb->runInEventBaseThread(
                      [req_up = std::move(req_up), q = std::move(q)]() mutable {
                        req_up->sendReply(std::move(q));
                    });
                  } catch (const std::exception& e) {
                    if (!oneway) {
                      req_up->sendErrorWrapped(
                          folly::make_exception_wrapper<TApplicationException>(
                            folly::to<std::string>(
                                "Failed to read response from Python:",
                                e.what())),
                          "python");
                    }
                  }
                });

                adapter_->attr("call_processor")(
                    input,
                    makePythonHeaders(context->getHeader()->getHeaders()),
                    int(clientType),
                    int(protType),
                    contextData,
                    callbackWrapper);
              }
            },
            preq, eb, oneway));
      req.release();
    } catch (const std::exception& e) {
      if (!oneway) {
        req->sendErrorWrapped(
            folly::make_exception_wrapper<TApplicationException>(
              "Failed to add task to queue, too full"),
            kQueueOverloadedErrorCode);
      }
    }
  }

  bool isOnewayMethod(const folly::IOBuf* buf, const THeader* header) override {
    auto protType = static_cast<apache::thrift::protocol::PROTOCOL_TYPES>
      (header->getProtocolId());
    switch (protType) {
      case apache::thrift::protocol::T_BINARY_PROTOCOL:
        return isOnewayMethod<apache::thrift::BinaryProtocolReader>(buf);
      case apache::thrift::protocol::T_COMPACT_PROTOCOL:
        return isOnewayMethod<apache::thrift::CompactProtocolReader>(buf);
      default:
        LOG(ERROR) << "Invalid protType: " << protType;
        return false;
    }
  }

private:
  template <typename ProtocolReader>
  bool isOnewayMethod(const folly::IOBuf* buf) {
    std::string fname;
    MessageType mtype;
    int32_t protoSeqId = 0;
    ProtocolReader iprot;
    iprot.setInput(buf);
    try {
      iprot.readMessageBegin(fname, mtype, protoSeqId);
      return onewayMethods_.find(fname) != onewayMethods_.end();
    } catch (const std::exception& ex) {
      LOG(ERROR) << "received invalid message from client: " << ex.what();
      return false;
    }
  }

  void getPythonOnewayMethods() {
    PyGILState_STATE state = PyGILState_Ensure();
    SCOPE_EXIT { PyGILState_Release(state); };
    object ret = adapter_->attr("oneway_methods")();
    if (ret.is_none()) {
      LOG(ERROR) << "Unexpected error in processor method";
      return;
    }
    tuple t = extract<tuple>(ret);
    for (int i = 0; i < len(t); i++) {
      onewayMethods_.insert(extract<std::string>(t[i]));
    }
  }

  std::shared_ptr<object> adapter_;
  std::unordered_set<std::string> onewayMethods_;
};

class PythonAsyncProcessorFactory : public AsyncProcessorFactory {
public:
  explicit PythonAsyncProcessorFactory(std::shared_ptr<object> adapter)
    : adapter_(adapter) {}

  std::unique_ptr<apache::thrift::AsyncProcessor> getProcessor() override {
    return folly::make_unique<PythonAsyncProcessor>(adapter_);
  }

private:
  std::shared_ptr<object> adapter_;
};

class CppServerWrapper : public ThriftServer {
public:
  void setAdapter(object adapter) {
    // We use a shared_ptr to manage the adapter so the processor
    // factory handing won't ever try to manipulate python reference
    // counts without the GIL.
    setProcessorFactory(
      folly::make_unique<PythonAsyncProcessorFactory>(
        std::make_shared<object>(adapter)));
  }

  // peer to setObserver, but since we want a different argument, avoid
  // shadowing in our parent class.
  void setObserverFromPython(object observer) {
    setObserver(std::make_shared<CppServerObserver>(observer));
  }

  object getAddress() {
    return makePythonAddress(ThriftServer::getAddress());
  }

  void loop() {
    PyThreadState* save_state = PyEval_SaveThread();
    SCOPE_EXIT { PyEval_RestoreThread(save_state); };

    // Thrift main loop.  This will run indefinitely, until stop() is
    // called.

    getServeEventBase()->loopForever();
  }

  void setCppSSLConfig(object sslConfig) {
    auto certPath = getStringAttrSafe(sslConfig, "cert_path");
    auto keyPath = getStringAttrSafe(sslConfig, "key_path");
    if (certPath.empty() ^ keyPath.empty()) {
      PyErr_SetString(PyExc_ValueError,
                      "certPath and keyPath must both be populated");
      throw_error_already_set();
      return;
    }
    auto cfg = std::make_shared<SSLContextConfig>();
    cfg->clientCAFile = getStringAttrSafe(sslConfig, "client_ca_path");
    if (!certPath.empty()) {
      auto keyPwPath = getStringAttrSafe(sslConfig, "key_pw_path");
      cfg->setCertificate(certPath, keyPath, keyPwPath);
    }
    cfg->clientVerification
      = extract<SSLContext::SSLVerifyPeerEnum>(sslConfig.attr("verify"));
    auto eccCurve = getStringAttrSafe(sslConfig, "ecc_curve_name");
    if (!eccCurve.empty()) {
      cfg->eccCurveName = eccCurve;
    }
    auto alpnProtocols = getStringListSafe(sslConfig, "alpn_protocols");
    cfg->setNextProtocols(alpnProtocols);
    object sessionContext = sslConfig.attr("session_context");
    if (!sessionContext.is_none()) {
      cfg->sessionContext = extract<std::string>(str(sessionContext));
    }

    ThriftServer::setSSLConfig(cfg);

    setSSLPolicy(extract<SSLPolicy>(sslConfig.attr("ssl_policy")));

    auto ticketFilePath = getStringAttrSafe(sslConfig, "ticket_file_path");
    tlsCredProcessor_.reset(); // stops the existing poller if any
    tlsCredProcessor_.reset(new TLSCredProcessor(ticketFilePath, certPath));
    tlsCredProcessor_->addTicketCallback(
        [this](wangle::TLSTicketKeySeeds seeds) {
      updateTicketSeeds(std::move(seeds));
    });
    auto seeds = TLSCredProcessor::processTLSTickets(ticketFilePath);
    if (seeds) {
      setTicketSeeds(std::move(*seeds));
    }
    tlsCredProcessor_->addCertCallback([this]() {
      updateTLSCert();
    });
  }

  void setCppFastOpenOptions(object enabledObj, object tfoMaxQueueObj) {
    bool enabled{extract<bool>(enabledObj)};
    uint32_t tfoMaxQueue{extract<uint32_t>(tfoMaxQueueObj)};
    ThriftServer::setFastOpenOptions(enabled, tfoMaxQueue);
  }

  void setCppSSLCacheOptions(object cacheOptions) {
    SSLCacheOptions options = {
        .sslCacheTimeout = std::chrono::seconds(
            getIntAttr<uint32_t>(cacheOptions, "ssl_cache_timeout_seconds")),
        .maxSSLCacheSize =
            getIntAttr<uint64_t>(cacheOptions, "max_ssl_cache_size"),
        .sslCacheFlushSize =
            getIntAttr<uint64_t>(cacheOptions, "ssl_cache_flush_size"),
    };
    ThriftServer::setSSLCacheOptions(std::move(options));
  }

  object getCppTicketSeeds() {
    auto seeds = getTicketSeeds();
    if (!seeds) {
      return boost::python::object();
    }
    boost::python::dict result;
    result["old"] = makePythonList(seeds->oldSeeds);
    result["current"] = makePythonList(seeds->currentSeeds);
    result["new"] = makePythonList(seeds->newSeeds);
    return result;
  }

  void cleanUp() {
    // Deadlock avoidance: consider a thrift worker thread is doing
    // something in C++-land having relinquished the GIL.  This thread
    // acquires the GIL, stops the workers, and waits for the worker
    // threads to complete.  The worker thread now finishes its work,
    // and tries to reacquire the GIL, but deadlocks with the current
    // thread, which holds the GIL and is waiting for the worker to
    // complete.  So we do cleanUp() without the GIL, and reacquire it
    // only once thrift is all cleaned up.

    PyThreadState* save_state = PyEval_SaveThread();
    SCOPE_EXIT { PyEval_RestoreThread(save_state); };
    tlsCredProcessor_.reset();
    ThriftServer::cleanUp();
  }

  void setIdleTimeout(int timeout) {
    std::chrono::milliseconds ms(timeout);
    ThriftServer::setIdleTimeout(ms);
  }

  void setTaskExpireTime(int timeout) {
    std::chrono::milliseconds ms(timeout);
    ThriftServer::setTaskExpireTime(ms);
  }

  void setCppServerEventHandler(object serverEventHandler) {
    setServerEventHandler(std::make_shared<CppServerEventHandler>(
          serverEventHandler));
  }

  void setNewSimpleThreadManager(
      size_t count,
      size_t pendingTaskCountMax,
      bool enableTaskStats,
      size_t maxQueueLen) {
    auto tm = ThreadManager::newSimpleThreadManager(
        count, pendingTaskCountMax, enableTaskStats, maxQueueLen);
    tm->threadFactory(std::make_shared<PosixThreadFactory>());
    tm->start();
    setThreadManager(std::move(tm));
  }

  // this adapts from a std::shared_ptr, which boost::python does not (yet)
  // support, to a boost::shared_ptr, which it has internal support for.
  //
  // the magic is in the custom deleter which takes and releases a refcount on
  // the std::shared_ptr, instead of doing any local deletion.
  boost::shared_ptr<ThreadManager>
  getThreadManagerHelper() {
    auto ptr = this->getThreadManager();
    return boost::shared_ptr<ThreadManager>(ptr.get(), [ptr](void*) {});
  }

 private:
  std::unique_ptr<TLSCredProcessor> tlsCredProcessor_;

};

BOOST_PYTHON_MODULE(CppServerWrapper) {
  PyEval_InitThreads();

  class_<CppContextData>("CppContextData")
    .def("getClientIdentity", &CppContextData::getClientIdentity)
    .def("getPeerAddress", &CppContextData::getPeerAddress)
    .def("getLocalAddress", &CppContextData::getLocalAddress)
    ;

  class_<CallbackWrapper, boost::noncopyable>("CallbackWrapper")
    .def("call", &CallbackWrapper::call)
    ;

  class_<CppServerWrapper, boost::noncopyable >(
    "CppServerWrapper")
    // methods added or customized for the python implementation
    .def("setAdapter", &CppServerWrapper::setAdapter)
    .def("setObserver", &CppServerWrapper::setObserverFromPython)
    .def("setIdleTimeout", &CppServerWrapper::setIdleTimeout)
    .def("setTaskExpireTime", &CppServerWrapper::setTaskExpireTime)
    .def("getAddress", &CppServerWrapper::getAddress)
    .def("loop", &CppServerWrapper::loop)
    .def("cleanUp", &CppServerWrapper::cleanUp)
    .def("setCppServerEventHandler",
         &CppServerWrapper::setCppServerEventHandler)
    .def("setNewSimpleThreadManager",
         &CppServerWrapper::setNewSimpleThreadManager)
    .def("setCppSSLConfig", &CppServerWrapper::setCppSSLConfig)
    .def("setCppSSLCacheOptions", &CppServerWrapper::setCppSSLCacheOptions)
    .def("setCppFastOpenOptions", &CppServerWrapper::setCppFastOpenOptions)
    .def("getCppTicketSeeds", &CppServerWrapper::getCppTicketSeeds)

    // methods directly passed to the C++ impl
    .def("setup", &CppServerWrapper::setup)
    .def("setNPoolThreads", &CppServerWrapper::setNPoolThreads)
    .def("setNWorkerThreads", &CppServerWrapper::setNWorkerThreads)
    .def("setNumCPUWorkerThreads", &CppServerWrapper::setNumCPUWorkerThreads)
    .def("setNumIOWorkerThreads", &CppServerWrapper::setNumIOWorkerThreads)
    .def("setListenBacklog", &CppServerWrapper::setListenBacklog)
    .def("setPort", &CppServerWrapper::setPort)
    .def("stop", &CppServerWrapper::stop)
    .def("setMaxConnections", &CppServerWrapper::setMaxConnections)
    .def("getMaxConnections", &CppServerWrapper::getMaxConnections)

    .def("getLoad", &CppServerWrapper::getLoad)
    .def("getRequestLoad", &CppServerWrapper::getRequestLoad)
    .def("getPendingCount", &CppServerWrapper::getPendingCount)
    .def("getActiveRequests", &CppServerWrapper::getActiveRequests)
    .def("getThreadManager", &CppServerWrapper::getThreadManagerHelper)
    ;

  class_<ThreadManager, boost::shared_ptr<ThreadManager>, boost::noncopyable>(
    "ThreadManager", no_init
  )
    .def("idleWorkerCount", &ThreadManager::idleWorkerCount)
    .def("workerCount", &ThreadManager::workerCount)
    .def("pendingTaskCount", &ThreadManager::pendingTaskCount)
    .def("totalTaskCount", &ThreadManager::totalTaskCount)
    .def("pendingTaskCountMax", &ThreadManager::pendingTaskCountMax)
    .def("expiredTaskCount", &ThreadManager::expiredTaskCount)
    ;

  class_<TServerObserver::CallTimestamps>("CallTimestamps", no_init)
    .def_readonly("readBegin", &TServerObserver::CallTimestamps::readBegin)
    .def_readonly("readEnd", &TServerObserver::CallTimestamps::readEnd)
    .def_readonly("processBegin", &TServerObserver::CallTimestamps::processBegin)
    .def_readonly("processEnd", &TServerObserver::CallTimestamps::processEnd)
    .def_readonly("writeBegin", &TServerObserver::CallTimestamps::writeBegin)
    .def_readonly("writeEnd", &TServerObserver::CallTimestamps::writeEnd)
    ;

  enum_<SSLPolicy>("SSLPolicy")
    .value("DISABLED", SSLPolicy::DISABLED)
    .value("PERMITTED", SSLPolicy::PERMITTED)
    .value("REQUIRED", SSLPolicy::REQUIRED)
    ;

  enum_<folly::SSLContext::SSLVerifyPeerEnum>("SSLVerifyPeerEnum")
    .value("VERIFY", folly::SSLContext::SSLVerifyPeerEnum::VERIFY)
    .value("VERIFY_REQ",
           folly::SSLContext::SSLVerifyPeerEnum::VERIFY_REQ_CLIENT_CERT)
    .value("NO_VERIFY", folly::SSLContext::SSLVerifyPeerEnum::NO_VERIFY)
    ;
}
