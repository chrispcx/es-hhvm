/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include <wangle/codec/StringCodec.h>
#include <wangle/codec/ByteToMessageDecoder.h>
#include <wangle/service/ClientDispatcher.h>
#include <wangle/service/ServerDispatcher.h>
#include <wangle/service/Service.h>
#include <wangle/service/CloseOnReleaseFilter.h>
#include <wangle/service/ExpiringFilter.h>

namespace wangle {

using namespace wangle;
using namespace folly;

typedef Pipeline<IOBufQueue&, std::string> ServicePipeline;

class SimpleDecode : public ByteToByteDecoder {
 public:
  bool decode(Context* ctx,
              IOBufQueue& buf,
              std::unique_ptr<IOBuf>& result,
              size_t&) override {
    result = buf.move();
    return result != nullptr;
  }
};

class EchoService : public Service<std::string, std::string> {
 public:
  Future<std::string> operator()(std::string req) override { return req; }
};

class EchoIntService : public Service<std::string, int> {
 public:
  Future<int> operator()(std::string req) override {
    return folly::to<int>(req);
  }
};

template <typename Req, typename Resp>
class ServerPipelineFactory
    : public PipelineFactory<ServicePipeline> {
 public:

  typename ServicePipeline::Ptr newPipeline(
      std::shared_ptr<AsyncTransportWrapper> socket) override {
    auto pipeline = ServicePipeline::create();
    pipeline->addBack(AsyncSocketHandler(socket));
    pipeline->addBack(SimpleDecode());
    pipeline->addBack(StringCodec());
    pipeline->addBack(SerialServerDispatcher<Req, Resp>(&service_));
    pipeline->finalize();
    return pipeline;
  }

 private:
  EchoService service_;
};

template <typename Req, typename Resp>
class ClientPipelineFactory : public PipelineFactory<ServicePipeline> {
 public:

  typename ServicePipeline::Ptr newPipeline(
      std::shared_ptr<AsyncTransportWrapper> socket) override {
    auto pipeline = ServicePipeline::create();
    pipeline->addBack(AsyncSocketHandler(socket));
    pipeline->addBack(SimpleDecode());
    pipeline->addBack(StringCodec());
    pipeline->finalize();
    return pipeline;
   }
};

template <typename Pipeline, typename Req, typename Resp>
class ClientServiceFactory : public ServiceFactory<Pipeline, Req, Resp> {
 public:
  class ClientService : public Service<Req, Resp> {
   public:
    explicit ClientService(Pipeline* pipeline) {
      dispatcher_.setPipeline(pipeline);
    }
    Future<Resp> operator()(Req request) override {
      return dispatcher_(std::move(request));
    }
   private:
    SerialClientDispatcher<Pipeline, Req, Resp> dispatcher_;
  };

  Future<std::shared_ptr<Service<Req, Resp>>> operator() (
    std::shared_ptr<ClientBootstrap<Pipeline>> client) override {
    return Future<std::shared_ptr<Service<Req, Resp>>>(
      std::make_shared<ClientService>(client->getPipeline()));
  }
};

TEST(Wangle, ClientServerTest) {
  int port = 1234;
  // server

  ServerBootstrap<ServicePipeline> server;
  server.childPipeline(
    std::make_shared<ServerPipelineFactory<std::string, std::string>>());
  server.bind(port);

  // client
  auto client = std::make_shared<ClientBootstrap<ServicePipeline>>();
  ClientServiceFactory<ServicePipeline, std::string, std::string> serviceFactory;
  client->pipelineFactory(
    std::make_shared<ClientPipelineFactory<std::string, std::string>>());
  SocketAddress addr("127.0.0.1", port);
  client->connect(addr);
  auto service = serviceFactory(client).value();
  auto rep = (*service)("test");

  rep.then([&](std::string value) {
    EXPECT_EQ("test", value);
    EventBaseManager::get()->getEventBase()->terminateLoopSoon();

  });
  EventBaseManager::get()->getEventBase()->loopForever();
  server.stop();
}

class AppendFilter : public ServiceFilter<std::string, std::string> {
 public:
  explicit AppendFilter(
    std::shared_ptr<Service<std::string, std::string>> service) :
      ServiceFilter<std::string, std::string>(service) {}

  Future<std::string> operator()(std::string req) override {
    return (*service_)(req + "\n");
  }
};

class IntToStringFilter
    : public ServiceFilter<int, int, std::string, std::string> {
 public:
  explicit IntToStringFilter(
    std::shared_ptr<Service<std::string, std::string>> service) :
      ServiceFilter<int, int, std::string, std::string>(service) {}

  Future<int> operator()(int req) override {
    return (*service_)(folly::to<std::string>(req)).then([](std::string resp) {
      return folly::to<int>(resp);
    });
  }
};

TEST(Wangle, FilterTest) {
  auto service = std::make_shared<EchoService>();
  auto filter = std::make_shared<AppendFilter>(service);
  auto result = (*filter)("test");
  EXPECT_EQ(result.value(), "test\n");
}

TEST(Wangle, ComplexFilterTest) {
  auto service = std::make_shared<EchoService>();
  auto filter = std::make_shared<IntToStringFilter>(service);
  auto result = (*filter)(1);
  EXPECT_EQ(result.value(), 1);
}

class ChangeTypeFilter
    : public ServiceFilter<int, std::string, std::string, int> {
 public:
  explicit ChangeTypeFilter(
    std::shared_ptr<Service<std::string, int>> service) :
      ServiceFilter<int, std::string, std::string, int>(service) {}

  Future<std::string> operator()(int req) override {
    return (*service_)(folly::to<std::string>(req)).then([](int resp) {
      return folly::to<std::string>(resp);
    });
  }
};

TEST(Wangle, SuperComplexFilterTest) {
  auto service = std::make_shared<EchoIntService>();
  auto filter = std::make_shared<ChangeTypeFilter>(service);
  auto result = (*filter)(1);
  EXPECT_EQ(result.value(), "1");
}

template <typename Pipeline, typename Req, typename Resp>
class ConnectionCountFilter : public ServiceFactoryFilter<Pipeline, Req, Resp> {
 public:
  explicit ConnectionCountFilter(
    std::shared_ptr<ServiceFactory<Pipeline, Req, Resp>> factory)
      : ServiceFactoryFilter<Pipeline, Req, Resp>(factory) {}

  Future<std::shared_ptr<Service<Req, Resp>>> operator()(
      std::shared_ptr<ClientBootstrap<Pipeline>> client) override {
      connectionCount++;
      return (*this->serviceFactory_)(client);
    }

  int connectionCount{0};
};

TEST(Wangle, ServiceFactoryFilter) {
  auto clientFactory =
    std::make_shared<
    ClientServiceFactory<ServicePipeline, std::string, std::string>>();
  auto countingFactory =
    std::make_shared<
    ConnectionCountFilter<ServicePipeline, std::string, std::string>>(
      clientFactory);

  auto client = std::make_shared<ClientBootstrap<ServicePipeline>>();

  client->pipelineFactory(
    std::make_shared<ClientPipelineFactory<std::string, std::string>>());
  // It doesn't matter if connect succeds or not, but it needs to be called
  // to create a pipeline
  client->connect(folly::SocketAddress("::1", 8090));

  auto service = (*countingFactory)(client).value();

  // After the first service goes away, the client can be reused
  service = (*countingFactory)(client).value();
  EXPECT_EQ(2, countingFactory->connectionCount);
}

TEST(Wangle, FactoryToService) {
  auto constfactory =
    std::make_shared<ConstFactory<ServicePipeline, std::string, std::string>>(
    std::make_shared<EchoService>());
  FactoryToService<ServicePipeline, std::string, std::string> service(
    constfactory);

  EXPECT_EQ("test", service("test").value());
}

class TimekeeperTester : public Timekeeper {
 public:
  virtual Future<Unit> after(Duration dur) {
    Promise<Unit> p;
    auto f = p.getFuture();
    promises_.push_back(std::move(p));
    return f;
  }
  template <class Clock>
  Future<Unit> at(std::chrono::time_point<Clock> when) {
    Promise<Unit> p;
    auto f = p.getFuture();
    promises_.push_back(std::move(p));
    return f;
  }
  std::vector<Promise<Unit>> promises_;
};

TEST(ServiceFilter, ExpiringMax) {
  TimekeeperTester timekeeper;

  std::shared_ptr<Service<std::string, std::string>> service =
    std::make_shared<EchoService>();
  std::shared_ptr<Service<std::string, std::string>> closeOnReleaseService =
    std::make_shared<CloseOnReleaseFilter<std::string, std::string>>(service);
  std::shared_ptr<Service<std::string, std::string>> expiringService =
    std::make_shared<ExpiringFilter<std::string, std::string>>(
      closeOnReleaseService,
      std::chrono::milliseconds(0),
      std::chrono::milliseconds(400),
      &timekeeper);

  EXPECT_EQ("test", (*expiringService)("test").get());
  timekeeper.promises_[0].setValue();
  EXPECT_TRUE((*expiringService)("test").getTry().hasException());
}

TEST(ServiceFilter, ExpiringIdle) {
  TimekeeperTester timekeeper;

  std::shared_ptr<Service<std::string, std::string>> service =
    std::make_shared<EchoService>();
  std::shared_ptr<Service<std::string, std::string>> closeOnReleaseService =
    std::make_shared<CloseOnReleaseFilter<std::string, std::string>>(service);
  std::shared_ptr<Service<std::string, std::string>> expiringService =
    std::make_shared<ExpiringFilter<std::string, std::string>>(
      closeOnReleaseService,
      std::chrono::milliseconds(100),
      std::chrono::milliseconds(0),
      &timekeeper);

  EXPECT_EQ(1, timekeeper.promises_.size());
}

TEST(ServiceFilter, NoIdleDuringRequests) {
  TimekeeperTester timekeeper;

  std::shared_ptr<Service<std::string, std::string>> service =
    std::make_shared<EchoService>();
  std::shared_ptr<Service<std::string, std::string>> closeOnReleaseService =
    std::make_shared<CloseOnReleaseFilter<std::string, std::string>>(service);
  std::shared_ptr<Service<std::string, std::string>> expiringService =
    std::make_shared<ExpiringFilter<std::string, std::string>>(
      closeOnReleaseService,
      std::chrono::milliseconds(1),
      std::chrono::milliseconds(0),
      &timekeeper);

  auto f = (*expiringService)("2000");
  EXPECT_EQ(2, timekeeper.promises_.size());
  f.get();
  EXPECT_EQ("2000", (*expiringService)("2000").get());
  EXPECT_EQ(3, timekeeper.promises_.size());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}

} // namespace wangle
