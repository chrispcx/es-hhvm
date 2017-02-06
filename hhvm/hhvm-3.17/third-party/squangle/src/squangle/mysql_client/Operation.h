/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
//
// Operation objects are the handles users of AsyncMysqlClient use to
// interact with connections and queries.  Every action a user
// initiates returns an Operation to track the status of the action,
// report errors, etc.  Operations also offer a way to set callbacks
// for completion.
//
// In general, operations are held in shared_ptr's as ownership is
// unclear.  This allows the construction of Operations, callbacks to
// be set, and the Operation itself be cleaned up by AsyncMysqlClient.
// Conversely, if callbacks aren't being used, the Operation can
// simply be wait()'d upon for completion.
//
// See README for examples.
//
// Implementation detail; Operations straddle the caller's thread and
// the thread managed by the AsyncMysqlClient.  They also are
// responsible for execution of the actual libmysqlclient functions
// and most interactions with libevent.
//
// As mentioned above, an Operation's lifetime is determined by both
// AsyncMysqlClient and the calling point that created an Operation.
// It is permissible to immediately discard an Operation or to hold
// onto it (via a shared_ptr).  However, all calls to methods such as
// result() etc must occur either in the callback or after wait() has
// returned.

#ifndef COMMON_ASYNC_MYSQL_OPERATION_H
#define COMMON_ASYNC_MYSQL_OPERATION_H

#include <mysql.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <folly/Exception.h>
#include <folly/Memory.h>
#include <folly/String.h>
#include <folly/dynamic.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/SSLContext.h>
#include <wangle/client/ssl/SSLSession.h>
#include "squangle/logger/DBEventLogger.h"
#include "squangle/mysql_client/Connection.h"
#include "squangle/mysql_client/DbResult.h"
#include "squangle/mysql_client/Query.h"
#include "squangle/mysql_client/Row.h"

namespace facebook {
namespace common {
namespace mysql_client {

using facebook::db::OperationStateException;

class MysqlHandler;
class MysqlClientBase;
class QueryResult;
class ConnectOperation;
class FetchOperation;
class MultiQueryStreamOperation;
class QueryOperation;
class MultiQueryOperation;
class Operation;
class Connection;
class ConnectionKey;
class ConnectionSocketHandler;
class ConnectionOptions;
class SSLOptionsProviderBase;
class SyncConnection;
class MultiQueryStreamHandler;

enum class QueryCallbackReason;

enum class StreamState;

// Simplify some std::chrono types.
typedef std::chrono::time_point<std::chrono::high_resolution_clock> Timepoint;

// Callbacks for connecting and querying, respectively.  A
// ConnectCallback is invoked when a connection succeeds or fails.  A
// QueryCallback is called for each row block (see Row.h) as well as
// when the query completes (either successfully or with an error).
typedef std::function<void(ConnectOperation&)> ConnectCallback;
// Callback for observer. I will be called for a completed operation,
// after the callback for the specific operation is called, if one is defined.
typedef std::function<void(Operation&)> ObserverCallback;
typedef std::function<void(QueryOperation&, QueryResult*, QueryCallbackReason)>
    QueryCallback;
typedef std::function<
    void(MultiQueryOperation&, QueryResult*, QueryCallbackReason)>
    MultiQueryCallback;

using std::vector;
using std::string;
// The state of the Operation.  In general, callers will see Unstarted
// (i.e., haven't yet called run()) or Completed (which may mean
// success or error; see OperationResult below).  Pending and
// Cancelling are not visible at times an outside caller might see
// them (since, once run() has been called, wait() must be called
// before inspecting other Operation attributes).
enum class OperationState {
  Unstarted,
  Pending,
  Cancelling,
  Completed,
};

// Once an operation is Completed, it has a result type, indicating
// what ultimately occurred.  These are self-explanatory.
enum class OperationResult {
  Unknown,
  Succeeded,
  Failed,
  Cancelled,
  TimedOut,
};

// For control flows in callbacks. This indicates the reason a callback was
// fired. When a pack of rows if fetched it is used RowsFetched to
// indicate that new rows are available. QueryBoundary means that the
// fetching for current query has completed successfully, and if any
// query failed (OperationResult is Failed) we use Failure. Success is for
// indicating that all queries have been successfully fetched.
enum class QueryCallbackReason { RowsFetched, QueryBoundary, Failure, Success };

enum class StreamState { InitQuery, RowsReady, QueryEnded, Failure, Success };

class ConnectionOptions {
 public:
  ConnectionOptions();

  // Each attempt to acquire a connection will take at maximum this duration.
  // Use setTotalTimeout if you want to limit the timeout for all attempts.
  ConnectionOptions& setTimeout(Duration dur) {
    connection_timeout_ = dur;
    return *this;
  }

  Duration getTimeout() const {
    return connection_timeout_;
  }

  ConnectionOptions& setQueryTimeout(Duration dur) {
    query_timeout_ = dur;
    return *this;
  }

  Duration getQueryTimeout() const {
    return query_timeout_;
  }

  ConnectionOptions& setSSLOptionsProvider(
      std::shared_ptr<SSLOptionsProviderBase> ssl_options_provider) {
    ssl_options_provider_ = ssl_options_provider;
    return *this;
  }

  std::shared_ptr<SSLOptionsProviderBase> getSSLOptionsProvider() const {
    return ssl_options_provider_;
  }

  SSLOptionsProviderBase* getSSLOptionsProviderPtr() const {
    return ssl_options_provider_.get();
  }

  ConnectionOptions& setConnectionAttribute(
      const string& attr,
      const string& value) {
    connection_attributes_[attr] = value;
    return *this;
  }

  const std::unordered_map<string, string>& connectionAttributes() const {
    return connection_attributes_;
  }

  ConnectionOptions& setConnectionAttributes(
      const std::unordered_map<string, string>& attributes) {
    connection_attributes_ = attributes;
    return *this;
  }

  bool useCompression() const {
    return use_compression_;
  }

  ConnectionOptions& setUseCompression(bool use_compression) {
    use_compression_ = use_compression;
    return *this;
  }

  // MySQL 5.6 connection attributes.  Sent at time of connect.
  const std::unordered_map<string, string>& getConnectionAttributes() const {
    return connection_attributes_;
  }

  // Sets the amount of attempts that will be tried in order to acquire the
  // connection. Each attempt will take at maximum the given timeout. To set
  // a global timeout that the operation shouldn't take more than, use
  // setTotalTimeout.
  ConnectionOptions& setConnectAttempts(uint32_t max_attempts) {
    max_attempts_ = max_attempts;
    return *this;
  }

  uint32_t getConnectAttempts() const {
    return max_attempts_;
  }

  // If this is not set, but regular timeout was, the TotalTimeout for the
  // operation will be the number of attempts times the primary timeout.
  // Set this if you have strict timeout needs.
  ConnectionOptions& setTotalTimeout(Duration dur) {
    total_timeout_ = dur;
    return *this;
  }

  Duration getTotalTimeout() const {
    return total_timeout_;
  }

  // If true, then when a query timesout, the client will open a new connection
  // and kill the running query via KILL QUERY <mysqlThreadId>. This should
  // usually not be used with a proxy since most proxies should handle timeouts.
  ConnectionOptions& setKillOnQueryTimeout(bool killOnQueryTimeout) {
    killOnQueryTimeout_ = killOnQueryTimeout;
    return *this;
  }

  bool getKillOnQueryTimeout() const {
    return killOnQueryTimeout_;
  }

 private:
  Duration connection_timeout_;
  Duration total_timeout_;
  Duration query_timeout_;
  std::shared_ptr<SSLOptionsProviderBase> ssl_options_provider_;
  std::unordered_map<string, string> connection_attributes_;
  bool use_compression_ = false;
  uint32_t max_attempts_ = 1;
  bool killOnQueryTimeout_ = false;
};

// The abstract base for our available Operations.  Subclasses share
// intimate knowledge with the Operation class (most member variables
// are protected).
class Operation : public std::enable_shared_from_this<Operation> {
 public:
  // No public constructor.
  virtual ~Operation();

  Operation* run();

  // Set a timeout; otherwise FLAGS_async_mysql_timeout_micros is
  // used.
  Operation* setTimeout(Duration timeout) {
    CHECK_THROW(state_ == OperationState::Unstarted, OperationStateException);
    timeout_ = timeout;
    return this;
  }

  Duration getTimeout() {
    return timeout_;
  }

  // Did the operation succeed?
  bool ok() const {
    return done() && result_ == OperationResult::Succeeded;
  }

  // Is the operation complete (success or failure)?
  bool done() const {
    return state_ == OperationState::Completed;
  }

  // host and port we are connected to (or will be connected to).
  const string& host() const;
  int port() const;

  // Try to cancel a pending operation.  This is inherently racey with
  // callbacks; it is possible the callback is being invoked *during*
  // the cancel attempt, so a cancelled operation may still succeed.
  void cancel();

  // Wait for the Operation to complete.
  void wait();

  // Wait for an operation to complete, and CHECK if it fails.  Mainly
  // for testing.
  virtual void mustSucceed() = 0;

  // Information about why this operation failed.
  int mysql_errno() const {
    return mysql_errno_;
  }
  const string& mysql_error() const {
    return mysql_error_;
  }
  const string& mysql_normalize_error() const {
    return mysql_normalize_error_;
  }

  // Get the state and result, as well as readable string versions.
  OperationResult result() const {
    return result_;
  }

  folly::StringPiece resultString() const;

  OperationState state() const {
    return state_;
  }

  folly::StringPiece stateString() const;

  static folly::StringPiece toString(OperationState state);
  static folly::StringPiece toString(OperationResult result);
  static folly::StringPiece toString(QueryCallbackReason reason);
  static folly::StringPiece toString(StreamState state);

  // An Operation can have a folly::dynamic associated with it.  This
  // can represent anything the caller wants to track and is primarily
  // useful in the callback.  Typically this would be a string or
  // integer.  Note, also, such information can be stored inside the
  // callback itself (via a lambda).
  Operation* setUserData(folly::dynamic val) {
    user_data_.assign(std::move(val));
    return this;
  }

  const folly::dynamic& userData() const {
    return *user_data_;
  }
  folly::dynamic&& stealUserData() {
    return std::move(*user_data_);
  }

  // Connections are transferred across operations.  At any one time,
  // there is one unique owner of the connection.
  std::unique_ptr<Connection>&& releaseConnection();
  Connection* connection() {
    return conn_proxy_.get();
  }

  // Various accessors for our Operation's start, end, and total elapsed time.
  Timepoint startTime() const {
    return start_time_;
  }
  Timepoint endTime() const {
    CHECK_THROW(state_ == OperationState::Completed, OperationStateException);
    return end_time_;
  }

  Duration elapsed() const {
    CHECK_THROW(state_ == OperationState::Completed, OperationStateException);
    return std::chrono::duration_cast<std::chrono::microseconds>(
        end_time_ - start_time_);
  }

  void setObserverCallback(ObserverCallback obs_cb);

  // Retrieve the shared pointer that holds this instance.
  std::shared_ptr<Operation> getSharedPointer();

  MysqlClientBase* client();

  // Flag internal async client errors; this always becomes a MySQL
  // error 2000 (CR_UNKNOWN_ERROR) with a suitable descriptive message.
  void setAsyncClientError(StringPiece msg, StringPiece normalizeMsg = "");

  virtual db::OperationType getOperationType() const = 0;

 protected:

  // Threshold is 500ms, but because of smoothing, actual last loop delay
  // needs to be roughly 2x this value to trigger detection
  static constexpr double kAvgLoopTimeStallThresholdUs = 500 * 1000;

  class ConnectionProxy;
  explicit Operation(ConnectionProxy&& conn);

  ConnectionProxy& conn() {
    return conn_proxy_;
  }
  const ConnectionProxy& conn() const {
    return conn_proxy_;
  }

  // Save any mysql errors that occurred (since we may hand off the
  // Connection before the user wants this information).
  void snapshotMysqlErrors();

  // Same as above, but specify the error code.
  void setAsyncClientError(
      int mysql_errno,
      StringPiece msg,
      StringPiece normalizeMsg = "");

  // Called when an Operation needs to wait for the socket to become
  // readable or writable (aka actionable).
  void waitForSocketActionable();

  // Overridden in child classes and invoked when the socket is
  // actionable.  This function should either completeOperation or
  // waitForSocketActionable.
  virtual void socketActionable() = 0;

  // Called by ConnectionSocketHandler when the operation timed out
  void timeoutTriggered();

  // Our operation has completed.  During completeOperation,
  // specializedCompleteOperation is invoked for subclasses to perform
  // their own finalization (typically annotating errors and handling
  // timeouts).
  void completeOperation(OperationResult result);
  void completeOperationInner(OperationResult result);
  virtual Operation* specializedRun() = 0;
  virtual void specializedTimeoutTriggered() = 0;
  virtual void specializedCompleteOperation() = 0;

  class OwnedConnection {
   public:
    OwnedConnection();
    explicit OwnedConnection(std::unique_ptr<Connection>&& conn);
    Connection* get();
    std::unique_ptr<Connection>&& releaseConnection();

   private:
    std::unique_ptr<Connection> conn_;
  };

  class ReferencedConnection {
   public:
    ReferencedConnection() : conn_(nullptr) {}
    explicit ReferencedConnection(Connection* conn) : conn_(conn) {}
    Connection* get() {
      return conn_;
    }

   private:
    Connection* conn_;
  };

  // Base class for a wrapper around the 2 types of connection
  // pointers we accept in the Operation:
  // - OwnedConnection: will hold an unique_ptr to the Connection
  //   for the async calls of the API, so the ownership is clear;
  // - ReferencedConnection: allows synchronous calls without moving unique_ptrs
  //   to the Operation;
  class ConnectionProxy {
   public:
    explicit ConnectionProxy(OwnedConnection&& conn);
    explicit ConnectionProxy(ReferencedConnection&& conn);

    Connection* get();

    std::unique_ptr<Connection>&& releaseConnection();

    const Connection* get() const {
      return const_cast<ConnectionProxy*>(this)->get();
    }

    Connection* operator->() {
      return get();
    }
    const Connection* operator->() const {
      return get();
    }

    ConnectionProxy(ConnectionProxy&&) = default;
    ConnectionProxy& operator=(ConnectionProxy&&) = default;

    ConnectionProxy(ConnectionProxy const&) = delete;
    ConnectionProxy& operator=(ConnectionProxy const&) = delete;

   private:
    OwnedConnection ownedConn_;
    ReferencedConnection referencedConn_;
  };

  bool isInEventBaseThread();
  // Data members; subclasses freely interact with these.
  OperationState state_;
  OperationResult result_;

  // Our client is not owned by us. It must outlive all active Operations.
  Duration timeout_;
  Timepoint start_time_;
  Timepoint end_time_;

  // Our Connection object.  Created by ConnectOperation and moved
  // into QueryOperations.
  ConnectionProxy conn_proxy_;

  // Errors that may have occurred.
  int mysql_errno_;
  string mysql_error_;
  string mysql_normalize_error_;

  // This mutex protects the operation cancel process when the state
  // is being checked in `run` and the operation is being cancelled in other
  // thread.
  std::mutex run_state_mutex_;

 private:
  folly::Optional<folly::dynamic> user_data_;
  ObserverCallback observer_callback_;
  std::unique_ptr<db::ConnectionContextBase> connection_context_;

  MysqlClientBase* mysql_client_;

  bool cancel_on_run_ = false;

  Operation() = delete;
  Operation(const Operation&) = delete;
  Operation& operator=(const Operation&) = delete;

  friend class Connection;
  friend class SyncConnection;
  friend class ConnectionSocketHandler;
};

// An operation representing a pending connection.  Constructed via
// AsyncMysqlClient::beginConnection.
class ConnectOperation : public Operation {
 public:
  virtual ~ConnectOperation();

  void setCallback(ConnectCallback cb) {
    connect_callback_ = cb;
  }

  const string& database() const {
    return conn_key_.db_name;
  }
  const string& user() const {
    return conn_key_.user;
  }

  const ConnectionKey& getConnectionKey() const {
    return conn_key_;
  }
  const ConnectionOptions& getConnectionOptions() const;
  const ConnectionKey* getKey() const {
    return &conn_key_;
  }

  // Get and set MySQL 5.6 connection attributes.
  ConnectOperation* setConnectionAttribute(
      const string& attr,
      const string& value);

  const std::unordered_map<string, string>& connectionAttributes() const {
    return conn_options_.getConnectionAttributes();
  }

  ConnectOperation* setConnectionAttributes(
      const std::unordered_map<string, string>& attributes);

  ConnectOperation* setSSLOptionsProviderBase(
      std::unique_ptr<SSLOptionsProviderBase> ssl_options_provider);
  ConnectOperation* setSSLOptionsProvider(
      std::shared_ptr<SSLOptionsProviderBase> ssl_options_provider);

  // Default timeout for queries created by the connection this
  // operation will create.
  ConnectOperation* setDefaultQueryTimeout(Duration t);
  ConnectOperation* setConnectionContext(
      std::unique_ptr<db::ConnectionContextBase>&& e) {
    CHECK_THROW(state_ == OperationState::Unstarted, OperationStateException);
    connection_context_ = std::move(e);
    return this;
  }

  db::ConnectionContextBase* getConnectionContext() {
    CHECK_THROW(state_ == OperationState::Unstarted, OperationStateException);
    return connection_context_.get();
  }

  // Don't call this; it's public strictly for AsyncMysqlClient to be
  // able to call make_shared.
  ConnectOperation(MysqlClientBase* mysql_client, ConnectionKey conn_key);

  void mustSucceed() override;

  // Overriding to narrow the return type
  // Each connect attempt will take at most this timeout to retry to acquire
  // the connection.
  ConnectOperation* setTimeout(Duration timeout);

  ConnectOperation* setUserData(folly::dynamic val) {
    Operation::setUserData(std::move(val));
    return this;
  }

  // Sets the total timeout that the connect operation will use.
  // Each attempt will take at most `setTimeout`. Use this in case
  // you have strong timeout restrictions but still want the connection to
  // retry.
  ConnectOperation* setTotalTimeout(Duration total_timeout);

  // Sets the number of attempts this operation will try to acquire a mysql
  // connection.
  ConnectOperation* setConnectAttempts(uint32_t max_attempts);

  uint32_t attemptsMade() const {
    return attempts_made_;
  }

  Duration getAttemptTimeout() const {
    return conn_options_.getTimeout();
  }

  // Set if we should open a new connection to kill a timed out query
  // Should not be used when connecting through a proxy
  ConnectOperation* setKillOnQueryTimeout(bool killOnQueryTimeout);

  bool getKillOnQueryTimeout() const {
    return conn_options_.getKillOnQueryTimeout();
  }

  ConnectOperation* setUseCompression(bool use_compression) {
    conn_options_.setUseCompression(use_compression);
    return this;
  }

  bool useCompression() const {
    return conn_options_.useCompression();
  }

  ConnectOperation* setConnectionOptions(const ConnectionOptions& conn_options);

  static constexpr Duration kMinimumViableConnectTimeout =
      std::chrono::microseconds(50);

  virtual db::OperationType getOperationType() const override {
    return db::OperationType::Connect;
  }

 protected:
  virtual void attemptFailed(OperationResult result);
  virtual void attemptSucceeded(OperationResult result);

  ConnectOperation* specializedRun() override;
  void socketActionable() override;
  void specializedTimeoutTriggered() override;
  void specializedCompleteOperation() override;

  // Removes the Client ref, it can be called by child classes without needing
  // to add them as friend classes of AsyncMysqlClient
  virtual void removeClientReference();

  bool shouldCompleteOperation(OperationResult result);

  wangle::SSLSessionPtr getSSLSession();

  uint32_t attempts_made_ = 0;

  ConnectionOptions conn_options_;

  // Context information for logging purposes.
  std::unique_ptr<db::ConnectionContextBase> connection_context_;

 private:
  void specializedRunImpl();

  void logConnectCompleted(OperationResult result);

  void maybeStoreSSLSession();

  const ConnectionKey conn_key_;

  int flags_;

  ConnectCallback connect_callback_;
  bool active_in_client_;

  friend class AsyncMysqlClient;
  friend class MysqlClientBase;
};

// A fetching operation (query or multiple queries) use the same primary
// actions. This is an abstract base for this kind of operation.
// FetchOperation controls the flow of fetching a result:
//  - When there are rows to be read, it will identify it and call the
//  subclasses
// for them to consume the state;
//  - When there are no rows to be read or an error happened, proper
//  notifications
// will be made as well.
// This is the only Operation that can be paused, and the pause should only be
// called from within `notify` calls. That will allow another thread to read the
// state.
class FetchOperation : public Operation {
 public:
  virtual ~FetchOperation() = default;
  void mustSucceed() override;

  // Return the query as it was sent to MySQL (i.e., for a single
  // query, the query itself, but for multiquery, all queries
  // combined).
  folly::fbstring getExecutedQuery() const {
    CHECK_THROW(state_ != OperationState::Unstarted, OperationStateException);
    return rendered_query_.toFbstring();
  }

  // Number of queries that succeed to execute
  int numQueriesExecuted() {
    CHECK_THROW(state_ != OperationState::Pending, OperationStateException);
    return num_queries_executed_;
  }

  // This class encapsulates the operations and access to the MySQL ResultSet.
  // When the consumer receives a notification for RowsFetched, it should
  // consume `rowStream`:
  //   while (rowStream->hasNext()) {
  //     EphemeralRow row = consumeRow();
  //   }
  // The state within RowStream is also used for FetchOperation to know whether
  // or not to go to next query.
  class RowStream {
   public:
    RowStream(
        MYSQL_RES* mysql_query_result,
        MysqlHandler* handler);

    EphemeralRow consumeRow();

    bool hasNext();

    EphemeralRowFields* getEphemeralRowFields() {
      return &row_fields_;
    }

    ~RowStream() = default;
    RowStream(RowStream&&) = default;
    RowStream& operator=(RowStream&&) = default;

   private:
    friend class FetchOperation;
    bool slurp();
    // user shouldn't take information from this
    bool hasQueryFinished() {
      return query_finished_;
    }
    uint64_t numRowsSeen() const {
      return num_rows_seen_;
    }

    bool query_finished_ = false;
    uint64_t num_rows_seen_ = 0;

    using MysqlResultDeleter =
        folly::static_function_deleter<MYSQL_RES, mysql_free_result>;
    using MysqlResultUniquePtr = std::unique_ptr<MYSQL_RES, MysqlResultDeleter>;

    // All memory lifetime is guaranteed by FetchOperation.
    MysqlResultUniquePtr mysql_query_result_ = nullptr;
    folly::Optional<EphemeralRow> current_row_;
    EphemeralRowFields row_fields_;
    MysqlHandler* handler_ = nullptr;
  };

  // Streaming calls. Should only be called when using the StreamCallback.
  // TODO#10716355: We shouldn't let these functions visible for non-stream
  // mode. Leaking for tests.
  uint64_t currentLastInsertId();
  uint64_t currentAffectedRows();

  int numCurrentQuery() const {
    return num_current_query_;
  }

  RowStream* rowStream();

  // Stalls the FetchOperation until `resume` is called.
  // This is used to allow another thread to access the socket functions.
  void pauseForConsumer();

  // Resumes the operation to the action it was before `pause` was called.
  // Should only be called after pause.
  void resume();

  int rows_received_ = 0;

 protected:
  MultiQuery queries_;

  FetchOperation* specializedRun() override;

  FetchOperation(ConnectionProxy&& conn, std::vector<Query>&& queries);
  FetchOperation(ConnectionProxy&& conn, MultiQuery&& multi_query);

  enum class FetchAction {
    StartQuery,
    InitFetch,
    Fetch,
    WaitForConsumer,
    CompleteQuery,
    CompleteOperation
  };

  void setFetchAction(FetchAction action);
  static folly::StringPiece toString(FetchAction action);

  // In socket actionable it is analyzed the action that is required to continue
  // the operation. For example, if the fetch action is StartQuery, it runs
  // query or requests more results depending if it had already ran or not the
  // query. The same process happens for the other FetchActions.
  // The action member can be changed in other member functions called in
  // socketActionable to keep the fetching flow running.
  void socketActionable() override;
  void specializedTimeoutTriggered() override;
  void specializedCompleteOperation() override;

  // Overridden in child classes and invoked when the Query fetching
  // has done specific actions that might be needed for report (callbacks,
  // store fetched data, initialize data).
  virtual void notifyInitQuery() = 0;
  virtual void notifyRowsReady() = 0;
  virtual void notifyQuerySuccess(bool more_results) = 0;
  virtual void notifyFailure(OperationResult result) = 0;
  virtual void notifyOperationCompleted(OperationResult result) = 0;

  bool cancel_ = false;

 private:
  friend class MultiQueryStreamHandler;
  void specializedRunImpl();
  void resumeImpl();
  // Checks if the current thread has access to stream, or result data.
  bool isStreamAccessAllowed();
  bool isPaused();

  // Asynchronously kill a currently running query, returns
  // before the query is killed
  void killRunningQuery();

  // Current query data
  folly::Optional<RowStream> current_row_stream_;
  bool query_executed_ = false;
  // TODO: Rename `executed` to `succeeded`
  int num_queries_executed_ = 0;
  // During a `notify` call, the consumer might want to know the index of the
  // current query, that's what `num_current_query_` is counting.
  int num_current_query_ = 0;

  uint64_t current_affected_rows_ = 0;
  uint64_t current_last_insert_id_ = 0;

  // When the Fetch gets paused, active fetch action moves to `WaitForConsumer`
  // and the action that got paused gets saved so tat `resume` can set it
  // properly afterwards.
  FetchAction active_fetch_action_ = FetchAction::StartQuery;
  FetchAction paused_action_ = FetchAction::StartQuery;

  folly::StringPiece rendered_query_;
};

// This operation only supports one mode: streaming callback. This is a
// simple layer on top of FetchOperation to adapt from `notify` to
// StreamCallback.
// This is an experimental class. Please don't use directly.
class MultiQueryStreamOperation : public FetchOperation {
 public:
  virtual ~MultiQueryStreamOperation() = default;

  typedef std::function<void(FetchOperation*, StreamState)> Callback;

  using StreamCallback = boost::variant<MultiQueryStreamHandler*, Callback>;

  void notifyInitQuery() override;
  void notifyRowsReady() override;
  void notifyQuerySuccess(bool more_results) override;
  void notifyFailure(OperationResult result) override;
  void notifyOperationCompleted(OperationResult result) override;

  // Overriding to narrow the return type
  MultiQueryStreamOperation* setTimeout(Duration timeout) {
    Operation::setTimeout(timeout);
    return this;
  }

  MultiQueryStreamOperation(
      ConnectionProxy&& connection,
      MultiQuery&& multi_query);

  MultiQueryStreamOperation(
      ConnectionProxy&& connection,
      std::vector<Query>&& queries);

  db::OperationType getOperationType() const override {
    return db::OperationType::MultiQueryStream;
  }

  template<typename C>
  void setCallback(C cb) {
    stream_callback_ = cb;
  }

 private:

  // wrapper to construct CallbackVistor and invoke the
  // right callback
  void invokeCallback(StreamState state);

  // Vistor to invoke the right callback depending on the type stored
  // in the variant 'stream_callback_'
  struct CallbackVisitor : public boost::static_visitor<> {
    CallbackVisitor(MultiQueryStreamOperation* op, StreamState state)
        : op_(op), state_(state) {}

    void operator()(MultiQueryStreamHandler* handler) const {
      DCHECK(op_ != nullptr);
      if (handler != nullptr) {
        handler->streamCallback(op_, state_);
      }
    }

    void operator()(Callback cb) const {
      DCHECK(op_ != nullptr);
      if (cb != nullptr) {
        cb(op_, state_);
      }
    }
   private:
    MultiQueryStreamOperation* op_;
    StreamState state_;
  };

  StreamCallback stream_callback_;
};

// An operation representing a query.  If a callback is set, it
// invokes the callback as rows arrive.  If there is no callback, it
// buffers all results into memory and makes them available as a
// RowBlock.  This is inefficient for large results.
//
// Constructed via Connection::beginQuery.
class QueryOperation : public FetchOperation {
 public:
  virtual ~QueryOperation() = default;

  void setCallback(QueryCallback cb) {
    buffered_query_callback_ = cb;
  }

  // Steal all rows.  Only valid if there is no callback.  Inefficient
  // for large result sets.
  QueryResult&& stealQueryResult() {
    CHECK_THROW(ok(), OperationStateException);
    return std::move(*query_result_);
  }

  const QueryResult& queryResult() const {
    CHECK_THROW(ok(), OperationStateException);
    return *query_result_;
  }

  // Returns the Query of this operation
  const Query& getQuery() const {
    return queries_.getQuery(0);
  }

  // Steal all rows.  Only valid if there is no callback.  Inefficient
  // for large result sets.
  vector<RowBlock>&& stealRows() {
    return query_result_->stealRows();
  }

  const vector<RowBlock>& rows() const {
    return query_result_->rows();
  }

  // Last insert id (aka mysql_insert_id).
  uint64_t lastInsertId() const {
    return query_result_->lastInsertId();
  }

  // Number of rows affected (aka mysql_affected_rows).
  uint64_t numRowsAffected() const {
    return query_result_->numRowsAffected();
  }

  void setQueryResult(QueryResult query_result) {
    query_result_ = folly::make_unique<QueryResult>(std::move(query_result));
  }

  // Don't call this; it's public strictly for Connection to be able
  // to call make_shared.
  QueryOperation(ConnectionProxy&& connection, Query&& query);

  // Overriding to narrow the return type
  QueryOperation* setTimeout(Duration timeout) {
    Operation::setTimeout(timeout);
    return this;
  }

  QueryOperation* setUserData(folly::dynamic val) {
    Operation::setUserData(std::move(val));
    return this;
  }

  db::OperationType getOperationType() const override {
    return db::OperationType::Query;
  }

 protected:

  void notifyInitQuery() override;
  void notifyRowsReady() override;
  void notifyQuerySuccess(bool more_results) override;
  void notifyFailure(OperationResult result) override;
  void notifyOperationCompleted(OperationResult result) override;

 private:
  QueryCallback buffered_query_callback_;
  std::unique_ptr<QueryResult> query_result_;
  friend class Connection;
};

// An operation representing a query with multiple statements.
// If a callback is set, it invokes the callback as rows arrive.
// If there is no callback, it buffers all results into memory
// and makes them available as a RowBlock.
// This is inefficient for large results.
//
// Constructed via Connection::beginMultiQuery.
class MultiQueryOperation : public FetchOperation {
 public:
  virtual ~MultiQueryOperation();

  // Set our callback.  This is invoked multiple times -- once for
  // every RowBatch and once, with nullptr for the RowBatch,
  // indicating the query is complete.
  void setCallback(MultiQueryCallback cb) {
    buffered_query_callback_ = cb;
  }

  // Steal all rows. Only valid if there is no callback. Inefficient
  // for large result sets.
  // Only call after the query has finished, don't use it inside callbacks
  std::vector<QueryResult>&& stealQueryResults() {
    CHECK_THROW(done(), OperationStateException);
    return std::move(query_results_);
  }

  // Only call this after the query has finished and don't use it inside
  // callbacks
  const vector<QueryResult>& queryResults() const {
    CHECK_THROW(done(), OperationStateException);
    return query_results_;
  }

  // Returns the Query for a query index.
  const Query& getQuery(int index) const {
    return queries_.getQuery(index);
  }

  void setQueryResults(std::vector<QueryResult> query_results) {
    query_results_ = std::move(query_results);
  }

  // Don't call this; it's public strictly for Connection to be able
  // to call make_shared.
  MultiQueryOperation(
      ConnectionProxy&& connection,
      std::vector<Query>&& queries);

  // Overriding to narrow the return type
  MultiQueryOperation* setTimeout(Duration timeout) {
    Operation::setTimeout(timeout);
    return this;
  }

  MultiQueryOperation* setUserData(folly::dynamic val) {
    Operation::setUserData(std::move(val));
    return this;
  }

  db::OperationType getOperationType() const override {
    return db::OperationType::MultiQuery;
  }

 protected:

  void notifyInitQuery() override;
  void notifyRowsReady() override;
  void notifyQuerySuccess(bool more_results) override;
  void notifyFailure(OperationResult result) override;
  void notifyOperationCompleted(OperationResult result) override;

  // Calls the FetchOperation specializedCompleteOperation and then does
  // callbacks if needed

 private:
  MultiQueryCallback buffered_query_callback_;

  // Storage fields for every statement in the query
  // Only to be used if there is no callback set.
  std::vector<QueryResult> query_results_;
  // Buffer to trans to `query_results_` and for buffered callback.
  std::unique_ptr<QueryResult> current_query_result_;

  int num_current_query_ = 0;

  friend class Connection;
};

// Helper function to build the result for a ConnectOperation in the sync mode.
// It will block the thread and return the acquired connection, in case of
// error, it will throw MysqlException as expected in the sync mode.
std::unique_ptr<Connection> blockingConnectHelper(
    std::shared_ptr<ConnectOperation>& conn_op);
}
}
} // facebook::common::mysql_client

#endif // COMMON_ASYNC_MYSQL_OPERATION_H
