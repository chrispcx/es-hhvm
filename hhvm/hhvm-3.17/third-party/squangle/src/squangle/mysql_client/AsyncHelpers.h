/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef COMMON_ASYNC_HELPER_H
#define COMMON_ASYNC_HELPER_H

#include <vector>

#include "squangle/mysql_client/AsyncMysqlClient.h"
#include "squangle/mysql_client/Operation.h"

namespace facebook {
namespace common {
namespace mysql_client {

// This will allow callback users to be able to read all rows in the end of
// operation (Success or Failure).
// The Appender callbacks will only be called for `Success` and `Failure`
// and pass the whole result of the query or multi query.
typedef std::function<void(QueryOperation&, QueryResult, QueryCallbackReason)>
    QueryAppenderCallback;
typedef std::function<
    void(MultiQueryOperation&, vector<QueryResult>, QueryCallbackReason)>
    MultiQueryAppenderCallback;

QueryCallback resultAppender(const QueryAppenderCallback& callback);

MultiQueryCallback resultAppender(const MultiQueryAppenderCallback& callback);
}
}
} // facebook::common::mysql_client

#endif // COMMON_ASYNC_HELPER
