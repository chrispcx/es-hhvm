/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "squangle/mysql_client/Operation.h"
#include <errmsg.h> // mysql
#include <openssl/ssl.h>

#include <folly/Memory.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <folly/portability/GFlags.h>

#include <wangle/client/ssl/SSLSession.h>
#include "squangle/mysql_client/AsyncMysqlClient.h"
#include "squangle/mysql_client/SSLOptionsProviderBase.h"

DEFINE_int64(
    async_mysql_timeout_micros,
    60 * 1000 * 1000,
    "default timeout, in micros, for mysql operations");

namespace facebook {
namespace common {
namespace mysql_client {

namespace chrono = std::chrono;

ConnectionOptions::ConnectionOptions()
    : connection_timeout_(FLAGS_async_mysql_timeout_micros),
      total_timeout_(FLAGS_async_mysql_timeout_micros),
      query_timeout_(FLAGS_async_mysql_timeout_micros) {}

Operation::Operation(ConnectionProxy&& safe_conn)
    : state_(OperationState::Unstarted),
      result_(OperationResult::Unknown),
      conn_proxy_(std::move(safe_conn)),
      mysql_errno_(0),
      observer_callback_(nullptr),
      mysql_client_(conn()->mysql_client_) {
  timeout_ = Duration(FLAGS_async_mysql_timeout_micros);
  conn()->resetActionable();
}

bool Operation::isInEventBaseThread() {
  return connection()->isInEventBaseThread();
}

Operation::~Operation() {}

void Operation::waitForSocketActionable() {
  DCHECK(isInEventBaseThread());

  MYSQL* mysql = conn()->mysql();
  uint16_t event_mask = 0;
  switch (mysql->net.async_blocking_state) {
    case NET_NONBLOCKING_READ:
      event_mask |= folly::EventHandler::READ;
      break;
    case NET_NONBLOCKING_WRITE:
    case NET_NONBLOCKING_CONNECT:
      event_mask |= folly::EventHandler::WRITE;
      break;
    default:
      LOG(FATAL) << "Unknown nonblocking status "
                 << mysql->net.async_blocking_state;
  }

  auto end = timeout_ + start_time_;
  auto now = chrono::high_resolution_clock::now();
  if (now >= end) {
    timeoutTriggered();
    return;
  }

  conn()->socketHandler()->scheduleTimeout(
      chrono::duration_cast<chrono::milliseconds>(end - now).count());
  conn()->socketHandler()->registerHandler(event_mask);
}

void Operation::cancel() {
  {
    // This code competes with `run()` to see who changes `state_` first,
    // since they both have the combination `check and change` this must
    // be locked
    std::unique_lock<std::mutex> l(run_state_mutex_);

    if (state_ == OperationState::Cancelling ||
        state_ == OperationState::Completed) {
      // If the cancel was already called we dont do the cancelling
      // process again
      return;
    }

    if (state_ == OperationState::Unstarted) {
      cancel_on_run_ = true;
      // wait the user to call "run()" to run the completeOperation
      // otherwise we will throw exception
      return;
    }
  }

  state_ = OperationState::Cancelling;
  if (!connection()->runInThread(
          this, &Operation::completeOperation, OperationResult::Cancelled)) {
    // if a strange error happen in EventBase , mark it cancelled now
    completeOperationInner(OperationResult::Cancelled);
  }
}

void Operation::timeoutTriggered() {
  specializedTimeoutTriggered();
}

Operation* Operation::run() {
  {
    std::unique_lock<std::mutex> l(run_state_mutex_);
    if (cancel_on_run_) {
      state_ = OperationState::Cancelling;
      cancel_on_run_ = false;
      connection()->runInThread(
          this, &Operation::completeOperation, OperationResult::Cancelled);
      return this;
    }
    CHECK_THROW(state_ == OperationState::Unstarted, OperationStateException);
    state_ = OperationState::Pending;
  }
  start_time_ = chrono::high_resolution_clock::now();
  return specializedRun();
}

void Operation::completeOperation(OperationResult result) {
  DCHECK(isInEventBaseThread());
  if (state_ == OperationState::Completed) {
    return;
  }

  CHECK_THROW(
      state_ == OperationState::Pending ||
          state_ == OperationState::Cancelling ||
          state_ == OperationState::Unstarted,
      OperationStateException);
  completeOperationInner(result);
}

void Operation::completeOperationInner(OperationResult result) {
  state_ = OperationState::Completed;
  result_ = result;
  end_time_ = chrono::high_resolution_clock::now();
  if ((result == OperationResult::Cancelled ||
       result == OperationResult::TimedOut) &&
      conn()->hasInitialized()) {
    // Cancelled/timed out ops leave our connection in an undefined
    // state.  Close it to prevent trouble.
    conn()->close();
  }

  conn()->socketHandler()->unregisterHandler();
  conn()->socketHandler()->cancelTimeout();

  specializedCompleteOperation();

  // call observer callback
  if (observer_callback_) {
    observer_callback_(*this);
  }

  client()->deferRemoveOperation(this);
}

std::unique_ptr<Connection>&& Operation::releaseConnection() {
  CHECK_THROW(
      state_ == OperationState::Completed ||
          state_ == OperationState::Unstarted,
      OperationStateException);
  return std::move(conn_proxy_.releaseConnection());
}

void Operation::snapshotMysqlErrors() {
  mysql_errno_ = ::mysql_errno(conn()->mysql());
  if (mysql_errno_ != 0) {
    mysql_error_ = ::mysql_error(conn()->mysql());
    mysql_normalize_error_ = mysql_error_;
  }
}

void Operation::setAsyncClientError(StringPiece msg, StringPiece normalizeMsg) {
  if (normalizeMsg.empty()) {
    normalizeMsg = msg;
  }
  mysql_errno_ = CR_UNKNOWN_ERROR;
  mysql_error_ = msg.toString();
  mysql_normalize_error_ = normalizeMsg.toString();
}

void Operation::setAsyncClientError(
    int mysql_errno,
    StringPiece msg,
    StringPiece normalizeMsg) {
  if (normalizeMsg.empty()) {
    normalizeMsg = msg;
  }
  mysql_errno_ = mysql_errno;
  mysql_error_ = msg.toString();
  mysql_normalize_error_ = normalizeMsg.toString();
}

void Operation::wait() {
  return conn()->wait();
}

MysqlClientBase* Operation::client() {
  return mysql_client_;
}

std::shared_ptr<Operation> Operation::getSharedPointer() {
  return shared_from_this();
}

const string& Operation::host() const {
  return conn()->host();
}
int Operation::port() const {
  return conn()->port();
}

void Operation::setObserverCallback(ObserverCallback obs_cb) {
  CHECK_THROW(state_ == OperationState::Unstarted, OperationStateException);
  // allow more callbacks to be set
  if (observer_callback_) {
    ObserverCallback old_obs_cb = observer_callback_;
    observer_callback_ = [obs_cb, old_obs_cb](Operation& op) {
      obs_cb(op);
      old_obs_cb(op);
    };
  } else {
    observer_callback_ = obs_cb;
  }
}

ConnectOperation::ConnectOperation(
    MysqlClientBase* mysql_client,
    ConnectionKey conn_key)
    : Operation(Operation::ConnectionProxy(Operation::OwnedConnection(
          mysql_client->createConnection(conn_key, nullptr)))),
      conn_key_(conn_key),
      flags_(CLIENT_MULTI_STATEMENTS),
      active_in_client_(true) {
  mysql_client->activeConnectionAdded(&conn_key_);
}

ConnectOperation* ConnectOperation::setConnectionOptions(
    const ConnectionOptions& conn_opts) {
  setTimeout(conn_opts.getTimeout());
  setDefaultQueryTimeout(conn_opts.getQueryTimeout());
  setConnectionAttributes(conn_opts.getConnectionAttributes());
  setConnectAttempts(conn_opts.getConnectAttempts());
  setTotalTimeout(conn_opts.getTotalTimeout());
  setKillOnQueryTimeout(conn_opts.getKillOnQueryTimeout());
  setUseCompression(conn_opts.useCompression());
  auto provider = conn_opts.getSSLOptionsProvider();
  if (provider) {
    setSSLOptionsProvider(std::move(provider));
  }
  return this;
}

const ConnectionOptions& ConnectOperation::getConnectionOptions() const {
  return conn_options_;
}

ConnectOperation* ConnectOperation::setConnectionAttribute(
    const string& attr,
    const string& value) {
  CHECK_THROW(state_ == OperationState::Unstarted, OperationStateException);
  conn_options_.setConnectionAttribute(attr, value);
  return this;
}

ConnectOperation* ConnectOperation::setConnectionAttributes(
    const std::unordered_map<string, string>& attributes) {
  conn_options_.setConnectionAttributes(attributes);
  return this;
}
ConnectOperation* ConnectOperation::setDefaultQueryTimeout(Duration t) {
  conn_options_.setQueryTimeout(t);
  return this;
}

ConnectOperation* ConnectOperation::setTimeout(Duration timeout) {
  conn_options_.setTimeout(timeout);
  Operation::setTimeout(timeout);
  return this;
}
ConnectOperation* ConnectOperation::setTotalTimeout(Duration total_timeout) {
  conn_options_.setTotalTimeout(total_timeout);
  Operation::setTimeout(min(timeout_, total_timeout));
  return this;
}
ConnectOperation* ConnectOperation::setConnectAttempts(uint32_t max_attempts) {
  conn_options_.setConnectAttempts(max_attempts);
  return this;
}
ConnectOperation* ConnectOperation::setKillOnQueryTimeout(
    bool killOnQueryTimeout) {
  conn_options_.setKillOnQueryTimeout(killOnQueryTimeout);
  return this;
}
ConnectOperation* ConnectOperation::setSSLOptionsProviderBase(
    std::unique_ptr<SSLOptionsProviderBase> ssl_options_provider) {
  LOG(ERROR) << "Using deprecated function";
  return this;
}
ConnectOperation* ConnectOperation::setSSLOptionsProvider(
    std::shared_ptr<SSLOptionsProviderBase> ssl_options_provider) {
  conn_options_.setSSLOptionsProvider(ssl_options_provider);
  return this;
}

bool ConnectOperation::shouldCompleteOperation(OperationResult result) {
  // Cancelled doesn't really get to this point, the Operation is forced to
  // complete by Operation, adding this check here just-in-case.
  if (attempts_made_ >= conn_options_.getConnectAttempts() ||
      result == OperationResult::Cancelled) {
    return true;
  }

  auto now =
      std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(1);
  if (now > start_time_ + conn_options_.getTotalTimeout()) {
    return true;
  }

  return false;
}

void ConnectOperation::attemptFailed(OperationResult result) {
  ++attempts_made_;
  if (shouldCompleteOperation(result)) {
    completeOperation(result);
    return;
  }

  logConnectCompleted(result);

  conn()->socketHandler()->unregisterHandler();
  conn()->socketHandler()->cancelTimeout();
  conn()->close();

  auto now = std::chrono::high_resolution_clock::now();
  // Adjust timeout
  auto timeout_attempt_based = conn_options_.getTimeout() +
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
  timeout_ = min(timeout_attempt_based, conn_options_.getTotalTimeout());

  specializedRun();
}

void ConnectOperation::attemptSucceeded(OperationResult result) {
  ++attempts_made_;
  completeOperation(result);
}

void ConnectOperation::specializedRunImpl() {
  if (attempts_made_ == 0) {
    conn()->initialize();
  } else {
    conn()->initMysqlOnly();
  }
  removeClientReference();
  if (!conn()->mysql()) {
    setAsyncClientError("connection initialization failed");
    attemptFailed(OperationResult::Failed);
    return;
  }

  mysql_options(conn()->mysql(), MYSQL_OPT_CONNECT_ATTR_RESET, 0);
  for (const auto& kv : conn_options_.getConnectionAttributes()) {
    mysql_options4(
        conn()->mysql(),
        MYSQL_OPT_CONNECT_ATTR_ADD,
        kv.first.c_str(),
        kv.second.c_str());
  }

  if (useCompression()) {
    mysql_options(conn()->mysql(), MYSQL_OPT_COMPRESS, nullptr);
  }

  auto provider = conn_options_.getSSLOptionsProviderPtr();
  if (provider && provider->setMysqlSSLOptions(conn()->mysql())) {
    if (connection_context_) {
      connection_context_->isSslConnection = true;
    }
  }

  if (!client()->getMysqlHandler().initConnect(
          conn()->mysql(), conn_key_, flags_)) {
    setAsyncClientError("mysql_real_connect_nonblocking_init failed");
    attemptFailed(OperationResult::Failed);
    return;
  }
  conn()->socketHandler()->setOperation(this);

  // connect is immediately "ready" to do one loop
  socketActionable();
}

ConnectOperation* ConnectOperation::specializedRun() {
  if (!connection()->runInThread(this, &ConnectOperation::specializedRunImpl)) {
    completeOperationInner(OperationResult::Failed);
  }
  return this;
}

ConnectOperation::~ConnectOperation() {
  removeClientReference();
}

void ConnectOperation::socketActionable() {
  DCHECK(isInEventBaseThread());
  int error;
  auto& handler = conn()->client()->getMysqlHandler();
  auto status =
      handler.connect(conn()->mysql(), error, conn_options_, conn_key_, flags_);
  auto fd = mysql_get_file_descriptor(conn()->mysql());
  if (status == MysqlHandler::DONE) {
    if (error == 0) {
      if (fd <= 0) {
        LOG(ERROR) << "Unexpected invalid file descriptor on completed, "
                   << "errorless connect.  fd=" << fd;
        setAsyncClientError(
            "mysql_get_file_descriptor returned an invalid "
            "descriptor");
        attemptFailed(OperationResult::Failed);
        return;
      } else {
        conn()->socketHandler()->changeHandlerFD(fd);
        conn()->mysqlConnection()->connectionOpened();
        attemptSucceeded(OperationResult::Succeeded);
        return;
      }
    } else {
      snapshotMysqlErrors();
      attemptFailed(OperationResult::Failed);
      return;
    }
  } else {
    if (fd <= 0) {
      LOG(ERROR) << "Unexpected invalid file descriptor on completed, "
                 << "pending connect.  fd=" << fd;
      setAsyncClientError(
          "mysql_get_file_descriptor returned an invalid "
          "descriptor");
      attemptFailed(OperationResult::Failed);
      return;
    } else {
      conn()->socketHandler()->changeHandlerFD(fd);
      waitForSocketActionable();
      return;
    }
  }
  LOG(DFATAL) << "should not reach here";
}

void ConnectOperation::specializedTimeoutTriggered() {
  auto delta = chrono::high_resolution_clock::now() - start_time_;
  int64_t delta_micros =
      chrono::duration_cast<chrono::microseconds>(delta).count();

  // Check for an overloaded EventBase
  auto avgLoopTimeUs = conn()->getEventBase()->getAvgLoopTime();
  if (avgLoopTimeUs < kAvgLoopTimeStallThresholdUs) {
    auto msg = folly::stringPrintf(
        "async connect to %s:%d timed out (took %.2fms)",
        host().c_str(),
        port(),
        delta_micros / 1000.0);
    setAsyncClientError(CR_SERVER_LOST, msg, "async connect to host timed out");
  } else {
    auto msg = folly::stringPrintf(
        "async connect to %s:%d timed out (loop stalled, avg loop time %.2fms)",
        host().c_str(),
        port(),
        avgLoopTimeUs / 1000.0);
    setAsyncClientError(
        msg, "async connect to host timed out (loop stalled)");
  }
  attemptFailed(OperationResult::TimedOut);
}

void ConnectOperation::logConnectCompleted(OperationResult result) {
  // If the connection wasn't initialized, it's because the operation
  // was cancelled before anything started, so we don't do the logs
  if (!conn()->hasInitialized()) {
    return;
  }
  auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time_ - start_time_);
  if (result == OperationResult::Succeeded) {
    client()->logConnectionSuccess(
        db::CommonLoggingData(getOperationType(), elapsed),
        *conn()->getKey(),
        connection_context_.get());
  } else {
    db::FailureReason reason = db::FailureReason::DATABASE_ERROR;
    if (result == OperationResult::TimedOut) {
      reason = db::FailureReason::TIMEOUT;
    } else if (result == OperationResult::Cancelled) {
      reason = db::FailureReason::CANCELLED;
    }
    client()->logConnectionFailure(
        db::CommonLoggingData(getOperationType(), elapsed),
        reason,
        *conn()->getKey(),
        conn()->mysql(),
        connection_context_.get());
  }
}

void ConnectOperation::maybeStoreSSLSession() {
  // if there is an ssl provider set
  auto provider = conn_options_.getSSLOptionsProviderPtr();
  if (!provider) {
    return;
  }

  // If connection was successful
  if (result_ != OperationResult::Succeeded || !conn()->hasInitialized()) {
    return;
  }

  if (provider->storeMysqlSSLSession(conn()->mysql())) {
    if (connection_context_) {
      connection_context_->sslSessionReused = true;
    }
    client()->stats()->incrReusedSSLSessions();
  }
}

void ConnectOperation::specializedCompleteOperation() {
  maybeStoreSSLSession();

  logConnectCompleted(result_);

  // If connection_initialized_ is false the only way to complete the
  // operation is by cancellation
  DCHECK(conn()->hasInitialized() || result_ == OperationResult::Cancelled);

  conn()->setConnectionOptions(conn_options_);
  conn()->setConnectionContext(std::move(connection_context_));

  conn()->notify();

  if (connect_callback_) {
    connect_callback_(*this);
    // Release callback since no other callbacks will be made
    connect_callback_ = nullptr;
  }
  // In case this operation didn't even get the chance to run, we still need
  // to remove the reference it added to the async client
  removeClientReference();
}

void ConnectOperation::mustSucceed() {
  run();
  wait();
  if (!ok()) {
    LOG(FATAL) << "Connect failed: " << mysql_error_;
  }
}

void ConnectOperation::removeClientReference() {
  if (active_in_client_) {
    // It's safe to call the client since we still have a ref counting
    // it won't die before it goes to 0
    active_in_client_ = false;
    client()->activeConnectionRemoved(&conn_key_);
  }
}

FetchOperation::FetchOperation(
    ConnectionProxy&& conn,
    std::vector<Query>&& queries)
    : Operation(std::move(conn)), queries_(std::move(queries)) {}

FetchOperation::FetchOperation(
    ConnectionProxy&& conn,
    MultiQuery&& multi_query)
    : Operation(std::move(conn)), queries_(std::move(multi_query)) {}

bool FetchOperation::isStreamAccessAllowed() {
  // XOR if isPaused or the caller is coming from IO Thread
  return isPaused() || isInEventBaseThread();
}

bool FetchOperation::isPaused() {
  return active_fetch_action_ == FetchAction::WaitForConsumer;
}

FetchOperation* FetchOperation::specializedRun() {
  if (!connection()->runInThread(this, &FetchOperation::specializedRunImpl)) {
    completeOperationInner(OperationResult::Failed);
  }

  return this;
}

void FetchOperation::specializedRunImpl() {
  try {
    rendered_query_ = queries_.renderQuery(conn()->mysql());
    socketActionable();
  } catch (std::invalid_argument& e) {
    setAsyncClientError(
        string("Unable to parse Query: ") + e.what(), "Unable to parse Query");
    completeOperation(OperationResult::Failed);
  }
}

FetchOperation::RowStream::RowStream(
    MYSQL_RES* mysql_query_result,
    MysqlHandler* handler)
    : mysql_query_result_(mysql_query_result),
      row_fields_(
          mysql_fetch_fields(mysql_query_result),
          mysql_num_fields(mysql_query_result)),
      handler_(handler) {}

EphemeralRow FetchOperation::RowStream::consumeRow() {
  if (!current_row_.hasValue()) {
    LOG(DFATAL) << "Illegal operation";
  }
  EphemeralRow eph_row(std::move(*current_row_));
  current_row_.clear();
  return eph_row;
}

bool FetchOperation::RowStream::hasNext() {
  // Slurp needs to happen after `consumeRow` has been called.
  // Because it will move the buffer.
  slurp();
  // First iteration
  return current_row_.hasValue();
}

bool FetchOperation::RowStream::slurp() {
  CHECK_THROW(mysql_query_result_ != nullptr, OperationStateException);
  if (current_row_.hasValue() || query_finished_) {
    return true;
  }
  MYSQL_ROW row;
  auto status = handler_->fetchRow(mysql_query_result_.get(), row);
  if (status == MysqlHandler::PENDING) {
    return false;
  }
  if (row == nullptr) {
    query_finished_ = true;
    return true;
  }
  unsigned long* field_lengths = mysql_fetch_lengths(mysql_query_result_.get());
  current_row_.assign(EphemeralRow(row, field_lengths, &row_fields_));
  return true;
}

void FetchOperation::setFetchAction(FetchAction action) {
  if (isPaused()) {
    paused_action_ = action;
  } else {
    active_fetch_action_ = action;
  }
}

uint64_t FetchOperation::currentLastInsertId() {
  CHECK_THROW(isStreamAccessAllowed(), OperationStateException);
  return current_last_insert_id_;
}

uint64_t FetchOperation::currentAffectedRows() {
  CHECK_THROW(isStreamAccessAllowed(), OperationStateException);
  return current_affected_rows_;
}

FetchOperation::RowStream* FetchOperation::rowStream() {
  CHECK_THROW(isStreamAccessAllowed(), OperationStateException);
  return current_row_stream_.get_pointer();
}

void FetchOperation::socketActionable() {
  DCHECK(isInEventBaseThread());
  DCHECK(active_fetch_action_ != FetchAction::WaitForConsumer);

  auto& handler = conn()->client()->getMysqlHandler();

  // This loop runs the fetch actions required to successfully execute query,
  // request next results, fetch results, identify errors and complete operation
  // and queries.
  // All callbacks are done in the `notify` methods that children must
  // override. During callbacks for actions `Fetch` and `CompleteQuery`,
  // the consumer is allowed to pause the operation.
  // Some actions may request an action above it (like CompleteQuery may request
  // StartQuery) this is why we use this loop.
  while (1) {
    // When the fetch action is StartQuery it means either we need to execute
    // the query or ask for new results.
    // Next Actions:
    //  - StartQuery: may continue with StartQuery if socket not actionable, in
    //                this case socketActionable is exited;
    //  - CompleteOperation: if it fails to execute query or request next
    //                       results.
    //  - InitFetch: no errors during results request, so we initiate fetch.
    if (active_fetch_action_ == FetchAction::StartQuery) {
      int error = 0;
      auto status = MysqlHandler::PENDING;

      if (query_executed_) {
        ++num_current_query_;
        status = handler.nextResult(conn()->mysql(), error);
      } else {
        status = handler.runQuery(conn()->mysql(), rendered_query_, error);
      }

      if (status == MysqlHandler::PENDING) {
        waitForSocketActionable();
        return;
      }

      current_last_insert_id_ = 0;
      current_affected_rows_ = 0;
      query_executed_ = true;
      if (error) {
        active_fetch_action_ = FetchAction::CompleteQuery;
      } else {
        active_fetch_action_ = FetchAction::InitFetch;
      }
    }

    // Prior fetch start we read the values that may indicate errors, rows to
    // fetch or not. The initialize from children classes is called either way
    // to signal that any other calls from now are regarding a new query.
    // Next Actions:
    //  - CompleteOperation: in case an error occurred
    //  - Fetch: there are rows to fetch in this query
    //  - CompleteQuery: no rows to fetch (complete query will read rowsAffected
    //                   and lastInsertId to add to result
    if (active_fetch_action_ == FetchAction::InitFetch) {
      auto* mysql_query_result = handler.getResult(conn()->mysql());
      auto num_fields = mysql_field_count(conn()->mysql());

      // Check to see if this an empty query or an error
      if (!mysql_query_result && num_fields > 0) {
        // Failure. CompleteQuery will read errors.
        active_fetch_action_ = FetchAction::CompleteQuery;
      } else {
        if (num_fields > 0) {
          current_row_stream_.assign(RowStream(mysql_query_result, &handler));
          active_fetch_action_ = FetchAction::Fetch;
        } else {
          active_fetch_action_ = FetchAction::CompleteQuery;
        }
        notifyInitQuery();
      }
    }

    // This action is going to stick around until all rows are fetched or an
    // error occurs. When the RowStream is ready, we notify the subclasses for
    // them to consume it.
    // If `pause` is called during the callback and the stream is consumed then,
    // `row_stream_` is checked and we skip to the next action `CompleteQuery`.
    // If row_stream_ isn't ready, we wait for socket actionable.
    // Next Actions:
    //  - Fetch: in case it needs to fetch more rows, we break the loop and wait
    //           for socketActionable to be called again
    //  - CompleteQuery: an error occurred or rows finished to fetch
    //  - WaitForConsumer: in case `pause` is called during `notifyRowsReady`
    if (active_fetch_action_ == FetchAction::Fetch) {
      DCHECK(current_row_stream_.hasValue());
      // Try to catch when the user didn't pause or consumed the rows
      if (current_row_stream_->current_row_.hasValue()) {
        // This should help
        LOG(ERROR) << "Rows not consumed. Perhaps missing `pause`?";
        cancel_ = true;
        active_fetch_action_ = FetchAction::CompleteQuery;
        continue;
      }

      // When the query finished, `is_ready` is true, but there are no rows.
      bool is_ready = current_row_stream_->slurp();
      if (!is_ready) {
        waitForSocketActionable();
        break;
      }
      if (current_row_stream_->hasQueryFinished()) {
        active_fetch_action_ = FetchAction::CompleteQuery;
      } else {
        notifyRowsReady();
      }
    }

    // In case the query has at least started and finished by error or not,
    // here the final checks and data are gathered for the current query.
    // It checks if any errors occurred during query, and call children classes
    // to deal with their specialized query completion.
    // If `pause` is called, then `paused_action_` will be already `StartQuery`
    // or `CompleteOperation`.
    // Next Actions:
    //  - StartQuery: There are more results and children is not opposed to it.
    //                QueryOperation child sets to CompleteOperation, since it
    //                is not supposed to receive more than one result.
    //  - CompleteOperation: In case an error occurred during query or there are
    //                       no more results to read.
    //  - WaitForConsumer: In case `pause` is called during notification.
    if (active_fetch_action_ == FetchAction::CompleteQuery) {
      snapshotMysqlErrors();

      bool more_results = false;
      if (mysql_errno_ != 0 || cancel_) {
        active_fetch_action_ = FetchAction::CompleteOperation;
      } else {
        current_last_insert_id_ = mysql_insert_id(conn()->mysql());
        current_affected_rows_ = mysql_affected_rows(conn()->mysql());
        more_results = mysql_more_results(conn()->mysql());
        active_fetch_action_ = more_results ? FetchAction::StartQuery
                                            : FetchAction::CompleteOperation;

        // Call it after setting the active_fetch_action_ so the child class can
        // decide if it wants to change the state

        if (current_row_stream_ && current_row_stream_->mysql_query_result_) {
          rows_received_ +=
              mysql_num_rows(current_row_stream_->mysql_query_result_.get());
        }
        ++num_queries_executed_;
        notifyQuerySuccess(more_results);
      }
      current_row_stream_.clear();
    }

    // Once this action is set, the operation is going to be completed no matter
    // the reason it was called. It exists the loop.
    if (active_fetch_action_ == FetchAction::CompleteOperation) {
      if (cancel_) {
        state_ = OperationState::Cancelling;
        completeOperation(OperationResult::Cancelled);
      } else if (mysql_errno_ != 0) {
        completeOperation(OperationResult::Failed);
      } else {
        completeOperation(OperationResult::Succeeded);
      }
      break;
    }

    // If `pause` is called during the operation callbacks, this the Action it
    // should come to.
    // It's not necessary to unregister the socket event,  so just cancel the
    // timeout and wait for `resume` to be called.
    if (active_fetch_action_ == FetchAction::WaitForConsumer) {
      conn()->socketHandler()->cancelTimeout();
      break;
    }
  }
}

void FetchOperation::pauseForConsumer() {
  DCHECK(isInEventBaseThread());
  DCHECK(state() == OperationState::Pending);

  paused_action_ = active_fetch_action_;
  active_fetch_action_ = FetchAction::WaitForConsumer;
}

void FetchOperation::resumeImpl() {
  CHECK_THROW(isPaused(), OperationStateException);

  // We should only allow pauses during fetch or between queries.
  // If we come back as RowsFetched and the stream has completed the query,
  // `socketActionable` will change the `active_fetch_action_` and we will
  // start the Query completion process.
  // When we pause between queries, the value of `paused_action_` is already
  // the value of the next states: StartQuery or CompleteOperation.
  active_fetch_action_ = paused_action_;
  // Leave timeout to be reset or checked when we hit
  // `waitForSocketActionable`
  socketActionable();
}

void FetchOperation::resume() {
  DCHECK(active_fetch_action_ == FetchAction::WaitForConsumer);
  connection()->runInThread(this, &FetchOperation::resumeImpl);
}

namespace {
void copyRowToRowBlock(RowBlock* block, const EphemeralRow& eph_row) {
  block->startRow();
  for (int i = 0; i < eph_row.numFields(); ++i) {
    if (eph_row.isNull(i)) {
      block->appendNull();
    } else {
      block->appendValue(eph_row[i]);
    }
  }
  block->finishRow();
}

RowBlock makeRowBlockFromStream(
    std::shared_ptr<RowFields> row_fields,
    FetchOperation::RowStream* row_stream) {
  RowBlock row_block(std::move(row_fields));
  // Consume row_stream
  while (row_stream->hasNext()) {
    auto eph_row = row_stream->consumeRow();
    copyRowToRowBlock(&row_block, eph_row);
  }
  return row_block;
}
}

void FetchOperation::specializedTimeoutTriggered() {
  DCHECK(active_fetch_action_ != FetchAction::WaitForConsumer);
  auto delta = chrono::high_resolution_clock::now() - start_time_;
  int64_t delta_micros =
      chrono::duration_cast<chrono::microseconds>(delta).count();
  std::string msg;

  if (conn()->getConnectionOptions().getKillOnQueryTimeout()) {
    killRunningQuery();
  }

  /*
   * Calling mysql_free_result currently tries to flush the socket
   * This is unnecessary as the socket will be cleaned up anyway and
   * blocking calls are used to clean up. By removing the MYSQL
   * handle here, we stop the socket from being flushed.
   *
   * The current problem with that flush is that it can cause
   * underflow if the socket read returns an async response,
   * which isn't correctly handled and results in buffer overrun
   *
   * We will move to mysql_free_result_nonblocking once it has
   * been thoroughly tested
   */
  if (rowStream() && rowStream()->mysql_query_result_) {
    rowStream()->mysql_query_result_->handle = nullptr;
  }

  std::string rows;
  if (rowStream() && rowStream()->numRowsSeen()) {
    rows = folly::sformat("%lu rows", rowStream()->numRowsSeen());
  } else {
    rows = "no rows seen";
  }

  auto avgLoopTimeUs = conn()->getEventBase()->getAvgLoopTime();
  if (avgLoopTimeUs < kAvgLoopTimeStallThresholdUs) {
    msg = folly::stringPrintf(
        "async query timed out (%s, took %.2fms)",
        rows.c_str(),
        delta_micros / 1000.0);
    setAsyncClientError(CR_NET_READ_INTERRUPTED, msg, "async query timed out");
  } else {
    msg = folly::stringPrintf(
        "async query timed out (%s, loop stalled, avg loop time %.2fms)",
        rows.c_str(),
        avgLoopTimeUs / 1000.0);
    setAsyncClientError(msg, "async query timed out (loop stalled)");
  }
  completeOperation(OperationResult::TimedOut);
}

void FetchOperation::specializedCompleteOperation() {
  // Stats for query
  if (result_ == OperationResult::Succeeded) {
    // set last successful query time to MysqlConnectionHolder
    conn()->setLastActivityTime(chrono::high_resolution_clock::now());
    db::QueryLoggingData logging_data(
        getOperationType(),
        elapsed(),
        num_queries_executed_,
        rendered_query_.toString(),
        rows_received_);
    client()->logQuerySuccess(logging_data, *conn().get());
  } else {
    db::FailureReason reason = db::FailureReason::DATABASE_ERROR;
    if (result_ == OperationResult::Cancelled) {
      reason = db::FailureReason::CANCELLED;
    } else if (result_ == OperationResult::TimedOut) {
      reason = db::FailureReason::TIMEOUT;
    }
    client()->logQueryFailure(
        db::QueryLoggingData(
            getOperationType(),
            elapsed(),
            num_queries_executed_,
            rendered_query_.toString(),
            rows_received_),
        reason,
        *conn().get());
  }

  if (result_ != OperationResult::Succeeded) {
    notifyFailure(result_);
  }
  // This frees the `Operation::wait()` call. We need to free it here because
  // callback can stealConnection and we can't notify anymore.
  conn()->notify();
  notifyOperationCompleted(result_);
}

void FetchOperation::mustSucceed() {
  run();
  wait();
  if (!ok()) {
    LOG(FATAL) << "Query failed: " << mysql_error_;
  }
}

void FetchOperation::killRunningQuery() {
  /*
   * Send kill command to terminate the current operation on the DB
   * Note that we use KILL <processlist_id> to kill the entire connection
   * In the event the DB is behind a proxy this will kill the persistent
   * connection the proxy is using, so ConnectionOptions::killQueryOnTimeout_
   * should always be false when accessing the DB through a proxy
   *
   * Note that there is a risk of a race condition in the event that a proxy
   * is used and a query from this client times out, then the query completes
   * almost immediately after the timeout and a proxy gives the persistent
   * connection to another client which begins a query on that connection
   * before this client is able to send the KILL query on a separate
   * proxy->db connection which then terminates the OTHER client's query
   */
  auto thread_id = conn()->mysqlThreadId();
  auto host = conn()->host();
  auto port = conn()->port();
  auto conn_op = client()
                     ->beginConnection(*conn()->getKey())
                     ->setConnectionOptions(conn()->getConnectionOptions());
  conn_op->setCallback([thread_id, host, port](ConnectOperation& conn_op) {
    if (conn_op.ok()) {
      auto op = Connection::beginQuery(
          conn_op.releaseConnection(), "KILL %d", thread_id);
      op->setCallback([thread_id, host, port](
          QueryOperation& /* unused */,
          QueryResult* /* unused */,
          QueryCallbackReason reason) {
        if (reason == QueryCallbackReason::Failure) {
          LOG(WARNING) << folly::format(
              "Failed to kill query in thread {} on {}:{}",
              thread_id,
              host,
              port);
        }
      });
      op->run();
    }
  });
  conn_op->run();
}

MultiQueryStreamOperation::MultiQueryStreamOperation(
    ConnectionProxy&& conn,
    MultiQuery&& multi_query)
    : FetchOperation(std::move(conn), std::move(multi_query)) {}

MultiQueryStreamOperation::MultiQueryStreamOperation(
    ConnectionProxy&& conn,
    std::vector<Query>&& queries)
    : FetchOperation(std::move(conn), std::move(queries)) {}

void MultiQueryStreamOperation::invokeCallback(StreamState reason) {
  // Construct a CallbackVistor object and pass to apply_vistor. It will
  // call the appropriate overaload of 'operator()' depending on the type
  // of callback stored in stream_callback_ i.e. either MultiQueryStreamHandler
  // or MultiQueryStreamOperation::Callback.
  boost::apply_visitor(CallbackVisitor(this, reason), stream_callback_);
}

void MultiQueryStreamOperation::notifyInitQuery() {
  invokeCallback(StreamState::InitQuery);
}

void MultiQueryStreamOperation::notifyRowsReady() {
  invokeCallback(StreamState::RowsReady);
}

void MultiQueryStreamOperation::notifyQuerySuccess(bool) {
  // Query Boundary, only for streaming to allow the user to read from the
  // connection.
  // This will allow pause in the end of the query. End of operations don't
  // allow.
  invokeCallback(StreamState::QueryEnded);
}

void MultiQueryStreamOperation::notifyFailure(OperationResult) {
  // Nop
}

void MultiQueryStreamOperation::notifyOperationCompleted(
    OperationResult result) {
  auto reason =
      (result == OperationResult::Succeeded ? StreamState::Success
                                            : StreamState::Failure);

  invokeCallback(reason);
  stream_callback_ = nullptr;
}

QueryOperation::QueryOperation(ConnectionProxy&& conn, Query&& query)
    : FetchOperation(std::move(conn), std::vector<Query>{std::move(query)}),
      query_result_(folly::make_unique<QueryResult>(0)) {}

void QueryOperation::notifyInitQuery() {
  auto* row_stream = rowStream();
  if (row_stream) {
    // Populate RowFields, this is the metadata of rows.
    query_result_->setRowFields(
        row_stream->getEphemeralRowFields()->makeBufferedFields());
  }
}

void QueryOperation::notifyRowsReady() {
  // QueryOperation acts as consumer of FetchOperation, and will buffer the
  // result.
  auto row_block =
      makeRowBlockFromStream(query_result_->getSharedRowFields(), rowStream());

  // Empty result set
  if (row_block.numRows() == 0) {
    return;
  }
  if (buffered_query_callback_) {
    query_result_->setPartialRows(std::move(row_block));
    buffered_query_callback_(
        *this, query_result_.get(), QueryCallbackReason::RowsFetched);
  } else {
    query_result_->appendRowBlock(std::move(row_block));
  }
}

void QueryOperation::notifyQuerySuccess(bool more_results) {
  if (more_results) {
    // Bad usage of QueryOperation, we are going to cancel the query
    cancel_ = true;
    setFetchAction(FetchAction::CompleteOperation);
  }

  query_result_->setOperationResult(OperationResult::Succeeded);
  query_result_->setNumRowsAffected(FetchOperation::currentAffectedRows());
  query_result_->setLastInsertId(FetchOperation::currentLastInsertId());

  query_result_->setPartial(false);

  // We are not going to make callback to user now since this only one query,
  // we make when we finish the operation
}

void QueryOperation::notifyFailure(OperationResult result) {
  // Next call will be to notify user
  query_result_->setOperationResult(result);
}

void QueryOperation::notifyOperationCompleted(OperationResult result) {
  query_result_->setOperationResult(result);

  auto reason =
      (result == OperationResult::Succeeded ? QueryCallbackReason::Success
                                            : QueryCallbackReason::Failure);

  if (buffered_query_callback_) {
    buffered_query_callback_(*this, query_result_.get(), reason);
    // Release callback since no other callbacks will be made
    buffered_query_callback_ = nullptr;
  }
}

MultiQueryOperation::MultiQueryOperation(
    ConnectionProxy&& conn,
    std::vector<Query>&& queries)
    : FetchOperation(std::move(conn), std::move(queries)),
      current_query_result_(folly::make_unique<QueryResult>(0)) {}

void MultiQueryOperation::notifyInitQuery() {
  auto* row_stream = rowStream();
  if (row_stream) {
    // Populate RowFields, this is the metadata of rows.
    current_query_result_->setRowFields(
        row_stream->getEphemeralRowFields()->makeBufferedFields());
  }
}

void MultiQueryOperation::notifyRowsReady() {
  // Create buffered RowBlock
  auto row_block = makeRowBlockFromStream(
      current_query_result_->getSharedRowFields(), rowStream());
  if (row_block.numRows() == 0) {
    return;
  }

  if (buffered_query_callback_) {
    current_query_result_->setPartialRows(std::move(row_block));
    buffered_query_callback_(
        *this, current_query_result_.get(), QueryCallbackReason::RowsFetched);
  } else {
    current_query_result_->appendRowBlock(std::move(row_block));
  }
}

void MultiQueryOperation::notifyFailure(OperationResult result) {
  // This needs to be called before notifyOperationCompleted, because
  // in non-callback mode we "notify" the conditional variable in `Connection`.
  current_query_result_->setOperationResult(result);
}

void MultiQueryOperation::notifyQuerySuccess(bool) {
  current_query_result_->setPartial(false);

  current_query_result_->setOperationResult(OperationResult::Succeeded);
  current_query_result_->setNumRowsAffected(
      FetchOperation::currentAffectedRows());
  current_query_result_->setLastInsertId(FetchOperation::currentLastInsertId());

  if (buffered_query_callback_) {
    buffered_query_callback_(
        *this, current_query_result_.get(), QueryCallbackReason::QueryBoundary);
  } else {
    query_results_.emplace_back(std::move(*current_query_result_.get()));
  }
  current_query_result_ =
      folly::make_unique<QueryResult>(current_query_result_->queryNum() + 1);
}

void MultiQueryOperation::notifyOperationCompleted(OperationResult result) {
  if (!buffered_query_callback_) { // No callback to be done
    return;
  }
  // Nothing that changes the non-callback state is safe to be done here.
  current_query_result_->setOperationResult(result);
  auto reason =
      (result == OperationResult::Succeeded ? QueryCallbackReason::Success
                                            : QueryCallbackReason::Failure);
  // If there was a callback, it fires now.
  if (buffered_query_callback_) {
    buffered_query_callback_(*this, current_query_result_.get(), reason);
    // Release callback since no other callbacks will be made
    buffered_query_callback_ = nullptr;
  }
}

MultiQueryOperation::~MultiQueryOperation() {}

folly::StringPiece Operation::resultString() const {
  return Operation::toString(result());
}

folly::StringPiece Operation::stateString() const {
  return Operation::toString(state());
}

folly::StringPiece Operation::toString(StreamState state) {
  switch (state) {
    case StreamState::InitQuery:
      return "InitQuery";
    case StreamState::RowsReady:
      return "RowsReady";
    case StreamState::QueryEnded:
      return "QueryEnded";
    case StreamState::Failure:
      return "Failure";
    case StreamState::Success:
      return "Success";
  }
  LOG(DFATAL) << "unable to convert state to string: "
              << static_cast<int>(state);
  return "Unknown state";
}

folly::StringPiece Operation::toString(QueryCallbackReason reason) {
  switch (reason) {
    case QueryCallbackReason::RowsFetched:
      return "RowsFetched";
    case QueryCallbackReason::QueryBoundary:
      return "QueryBoundary";
    case QueryCallbackReason::Failure:
      return "Failure";
    case QueryCallbackReason::Success:
      return "Success";
  }
  LOG(DFATAL) << "unable to convert reason to string: "
              << static_cast<int>(reason);
  return "Unknown reason";
}

folly::StringPiece Operation::toString(OperationState state) {
  switch (state) {
    case OperationState::Unstarted:
      return "Unstarted";
    case OperationState::Pending:
      return "Pending";
    case OperationState::Cancelling:
      return "Cancelling";
    case OperationState::Completed:
      return "Completed";
  }
  LOG(DFATAL) << "unable to convert state to string: "
              << static_cast<int>(state);
  return "Unknown state";
}

folly::StringPiece Operation::toString(OperationResult result) {
  switch (result) {
    case OperationResult::Succeeded:
      return "Succeeded";
    case OperationResult::Unknown:
      return "Unknown";
    case OperationResult::Failed:
      return "Failed";
    case OperationResult::Cancelled:
      return "Cancelled";
    case OperationResult::TimedOut:
      return "TimedOut";
  }
  LOG(DFATAL) << "unable to convert result to string: "
              << static_cast<int>(result);
  return "Unknown result";
}

folly::StringPiece FetchOperation::toString(FetchAction action) {
  switch (action) {
    case FetchAction::StartQuery:
      return "StartQuery";
    case FetchAction::InitFetch:
      return "InitFetch";
    case FetchAction::Fetch:
      return "Fetch";
    case FetchAction::WaitForConsumer:
      return "WaitForConsumer";
    case FetchAction::CompleteQuery:
      return "CompleteQuery";
    case FetchAction::CompleteOperation:
      return "CompleteOperation";
  }
  LOG(DFATAL) << "unable to convert result to string: "
              << static_cast<int>(action);
  return "Unknown result";
}

std::unique_ptr<Connection> blockingConnectHelper(
    std::shared_ptr<ConnectOperation>& conn_op) {
  conn_op->run()->wait();
  if (!conn_op->ok()) {
    throw MysqlException(
        conn_op->result(),
        conn_op->mysql_errno(),
        conn_op->mysql_error(),
        *conn_op->getKey(),
        conn_op->elapsed());
  }

  return std::move(conn_op->releaseConnection());
}

Operation::OwnedConnection::OwnedConnection() {}

Operation::OwnedConnection::OwnedConnection(std::unique_ptr<Connection>&& conn)
    : conn_(std::move(conn)) {}

Connection* Operation::OwnedConnection::get() {
  return conn_.get();
}

std::unique_ptr<Connection>&& Operation::OwnedConnection::releaseConnection() {
  return std::move(conn_);
}

Operation::ConnectionProxy::ConnectionProxy(Operation::OwnedConnection&& conn)
    : ownedConn_(std::move(conn)) {}

Operation::ConnectionProxy::ConnectionProxy(
    Operation::ReferencedConnection&& conn)
    : referencedConn_(std::move(conn)) {}

Connection* Operation::ConnectionProxy::get() {
  return ownedConn_.get() ? ownedConn_.get() : referencedConn_.get();
}

std::unique_ptr<Connection>&& Operation::ConnectionProxy::releaseConnection() {
  if (ownedConn_.get() != nullptr) {
    return ownedConn_.releaseConnection();
  }
  throw std::runtime_error("Releasing connection from referenced conn");
}
}
}
} // namespace facebook::common::mysql_client
