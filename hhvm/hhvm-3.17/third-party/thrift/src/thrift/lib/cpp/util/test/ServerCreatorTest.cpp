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

#include <thrift/lib/cpp/ClientUtil.h>
#include <thrift/lib/cpp/server/TServer.h>
#include <thrift/lib/cpp/util/ScopedServerThread.h>
#include <thrift/lib/cpp/util/TEventServerCreator.h>
#include <thrift/lib/cpp/util/example/TSimpleServerCreator.h>
#include <thrift/lib/cpp/util/TThreadedServerCreator.h>
#include <thrift/lib/cpp/util/example/TThreadPoolServerCreator.h>
#include <thrift/perf/if/gen-cpp/LoadTest.h>
#include <thrift/perf/cpp/AsyncLoadHandler.h>
#include <thrift/perf/cpp/LoadHandler.h>

#include <iostream>
#include <gtest/gtest.h>

using namespace apache::thrift::server;
using namespace apache::thrift::transport;
using namespace apache::thrift::test;
using namespace apache::thrift::util;
using std::string;
using std::cerr;
using std::endl;

///////////////////////////////////////////////////////////////////////////
// Test cases
//////////////////////////////////////////////////////////////////////////

void checkLoadServer(const folly::SocketAddress* address, bool framed = true) {
  std::shared_ptr<LoadTestClient> client =
    createClientPtr<LoadTestClient>(address, framed);

  string input = "foobar";
  string output;
  client->echo(output, input);

  EXPECT_EQ("foobar", output);
}

/*
 * Really basic tests to verify that we can start a server and
 * send it a request
 */

template<typename ServerCreatorT, typename HandlerT, typename ProcessorT>
void testServerCreator() {
  std::shared_ptr<HandlerT> handler(new HandlerT);
  // TODO: the generated code should make it possible to automatically create a
  // default processor from a specified handler.  This way we wouldn't have to
  // explicitly create a processor.
  std::shared_ptr<ProcessorT> processor(new ProcessorT(handler));

  // Start the server thread
  ServerCreatorT serverCreator(processor, 0);
  ScopedServerThread st(&serverCreator);

  // Make sure the server is running
  checkLoadServer(st.getAddress());
}

template<typename ServerCreatorT>
void testServerCreator() {
  testServerCreator<ServerCreatorT, LoadHandler, LoadTestProcessor>();
}

TEST(ServerCreatorTest, SimpleServer) {
  // "Testing TSimpleServerCreator"
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  testServerCreator<TSimpleServerCreator>();
  #pragma GCC diagnostic pop
}

TEST(ServerCreatorTest, ThreadedServer) {
  testServerCreator<TThreadedServerCreator>();
}

TEST(ServerCreatorTest, ThreadPoolServer) {
  // "Testing TThreadPoolServerCreator"
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  testServerCreator<TThreadPoolServerCreator>();
  #pragma GCC diagnostic pop
}

TEST(ServerCreatorTest, EventServerTaskQueueMode) {
  testServerCreator<TEventServerCreator>();
}

TEST(ServerCreatorTest, EventServerNativeMode) {
  testServerCreator<TEventServerCreator, AsyncLoadHandler,
                    LoadTestAsyncProcessor>();
}

/*
 * Test server behavior if we can't bind to the requested port
 */

template<typename ServerCreatorT, typename HandlerT, typename ProcessorT>
void testBindFailure() {
  std::shared_ptr<HandlerT> handler(new HandlerT);
  std::shared_ptr<ProcessorT> processor(new ProcessorT(handler));

  // Start a server thread
  ServerCreatorT serverCreator(processor, 0);
  ScopedServerThread st(&serverCreator);

  // Double check that the first server is running
  checkLoadServer(st.getAddress());

  // Try to start a second server listening on the same port
  ServerCreatorT serverCreator2(processor, st.getAddress()->getPort());
  std::shared_ptr<TServer> server = serverCreator2.createServer();

  // Call serve() and verify that it throws an exception
  // TODO: We should set some sort of timeout, so that if an error occurs and
  // the server does manage to start, we won't block forever.
  try {
    server->serve();
    ADD_FAILURE() << "we expected bind() to fail, but the server returned "
                  << "successfully from serve()";
  } catch (const TTransportException& ex) {
    EXPECT_EQ(TTransportException::COULD_NOT_BIND, ex.getType());
  } catch (const std::system_error& ex) {
    EXPECT_EQ(EADDRINUSE, ex.code().value());
  }
}

template<typename ServerCreatorT>
void testBindFailure() {
  testBindFailure<ServerCreatorT, LoadHandler, LoadTestProcessor>();
}

TEST(ServerCreatorTest, SimpleServerBindFailure) {
  // "Testing TSimpleServerCreator"
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  testBindFailure<TSimpleServerCreator>();
  #pragma GCC diagnostic pop
}

TEST(ServerCreatorTest, ThreadedServerBindFailure) {
  testBindFailure<TThreadedServerCreator>();
}

TEST(ServerCreatorTest, ThreadPoolServerBindFailure) {
  // "TestingTThreadPoolServerCreator"
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  testBindFailure<TThreadPoolServerCreator>();
  #pragma GCC diagnostic pop
}

TEST(ServerCreatorTest, EventServerBindFailure) {
  testBindFailure<TEventServerCreator, AsyncLoadHandler,
                  LoadTestAsyncProcessor>();
}

/*
 * Make sure ScopedServerThread raises an exception in the original thread
 * if it fails to start the server.
 */

template<typename ServerCreatorT, typename HandlerT, typename ProcessorT>
void testThreadedBindFailure() {
  std::shared_ptr<HandlerT> handler(new HandlerT);
  std::shared_ptr<ProcessorT> processor(new ProcessorT(handler));

  // Start a server thread
  ServerCreatorT serverCreator(processor, 0);
  ScopedServerThread st(&serverCreator);

  // Double check that the first server is running
  checkLoadServer(st.getAddress());

  // Try to start a second server listening on the same port
  ScopedServerThread st2;
  try {
    ServerCreatorT serverCreator2(processor, st.getAddress()->getPort());
    st2.start(&serverCreator2);
    ADD_FAILURE() << "we expected bind() to fail, but the server thread "
                  << "started successfully";
  } catch (const TTransportException& ex) {
    // Serve should throw a TTransportException
    EXPECT_EQ(TTransportException::COULD_NOT_BIND, ex.getType());
  } catch (const std::system_error& ex) {
    EXPECT_EQ(EADDRINUSE, ex.code().value());
  }
}

template<typename ServerCreatorT>
void testThreadedBindFailure() {
  return testThreadedBindFailure<ServerCreatorT, LoadHandler,
                                 LoadTestProcessor>();
}

TEST(ServerCreatorTest, SimpleServerThreadedBindFailure) {
  // "Testing TSimpleServerCreator"
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  testThreadedBindFailure<TSimpleServerCreator>();
  #pragma GCC diagnostic pop
}

TEST(ServerCreatorTest, ThreadedServerThreadedBindFailure) {
  testThreadedBindFailure<TThreadedServerCreator>();
}

TEST(ServerCreatorTest, ThreadPoolServerThreadedBindFailure) {
  // "Testing TThreadPoolServerCreator"
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  testThreadedBindFailure<TThreadPoolServerCreator>();
  #pragma GCC diagnostic pop
}

TEST(ServerCreatorTest, EventServerThreadedBindFailure) {
  testThreadedBindFailure<TEventServerCreator, AsyncLoadHandler,
                          LoadTestAsyncProcessor>();
}
