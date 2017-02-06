/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <iostream>

#include <thrift/lib/cpp2/server/ThriftServer.h>

#include <thrift/tutorial/cpp/stateful/ServiceAuthState.h>
#include <thrift/tutorial/cpp/stateful/ShellHandler.h>

using namespace std;
using namespace folly;
using namespace apache::thrift;
using namespace apache::thrift::tutorial::stateful;

int main(int argc, char* argv[]) {
  uint16_t port = 12345;

  auto authState = make_shared<ServiceAuthState>();
  auto handler = make_shared<ShellHandlerFactory>(authState);
  auto server = make_shared<ThriftServer>();
  server->setProcessorFactory(handler);
  server->setPort(port);

  cout << "serving on port " << port << "..." << endl;
  server->serve();
  return 0;
}
