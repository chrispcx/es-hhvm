/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
// @nolint
#ifndef MCROUTER_OPTION_GROUP
#define MCROUTER_OPTION_GROUP(_sep)
#endif

#define no_long ""
#define no_short '\0'

/**
 * Format:
 *
 * mcrouter_option_<string, integer, or toggle>(
 *  [type (integers only), ] name of field in the struct, default value,
 *  long option (or no_long), short option char (or no_short),
 *  docstring)
 *
 * A long option is a requirement for options that can be set from command line.
 *
 * Short options are optional and in short supply (pun overload).
 *
 * A toggle option doesn't accept a command line argument, and specifying
 * it on the command line will set it to the opposite of the default value.
 *
 * MCROUTER_OPTION_GROUP(name) starts a new option group (for nicer display)
 */

MCROUTER_OPTION_GROUP("Startup")

MCROUTER_OPTION_STRING(
  service_name, "unknown",
  no_long, no_short,
  "Name of the service using this libmcrouter instance")

MCROUTER_OPTION_STRING(
  router_name, "unknown",
  no_long, no_short,
  "Name for this router instance (should reflect the configuration,"
  " the flavor name is usually a good choice)")

MCROUTER_OPTION_STRING(
  flavor_name, "unknown",
  no_long, no_short,
  "Name of the flavor used to configure this router instance.")

MCROUTER_OPTION_TOGGLE(
  asynclog_disable, false,
  "asynclog-disable", no_short,
  "disable async log file spooling")

MCROUTER_OPTION_STRING(
  async_spool, "/var/spool/mcrouter",
  "async-dir", 'a',
  "container directory for async storage spools")

MCROUTER_OPTION_TOGGLE(
  use_asynclog_version2, false,
  "use-asynclog-version2", no_short,
  "Enable using the asynclog version 2.0")

MCROUTER_OPTION_INTEGER(
  size_t, num_proxies, DEFAULT_NUM_PROXIES,
  "num-proxies", no_short,
  "adjust how many proxy threads to run")

MCROUTER_OPTION_INTEGER(
  size_t, client_queue_size, 1024,
  no_long, no_short,
  "McrouterClient -> ProxyThread queue size.")

MCROUTER_OPTION_INTEGER(
  size_t, client_queue_no_notify_rate, 0,
  no_long, no_short,
  "Each client will only notify on every Nth request."
  "  If 0, normal notification logic is used - i.e. notify on every request,"
  " best effort avoid notifying twice.  Higher values decrease CPU utilization,"
  " but increase average latency.")

MCROUTER_OPTION_INTEGER(
  size_t, client_queue_wait_threshold_us, 0,
  no_long, no_short,
  "Force client queue notification if last drain was at least this long ago."
  "  If 0, this logic is disabled.")

MCROUTER_OPTION_INTEGER(
  size_t, big_value_split_threshold, 0,
  "big-value-split-threshold", no_short,
  "If 0, big value route handle is not part of route handle tree,"
  "else used as threshold for splitting big values internally")

MCROUTER_OPTION_INTEGER(
  size_t, big_value_batch_size, 10,
  "big-value-batch-size", no_short,
  "If nonzero, big value chunks are written/read in batches of at most"
  " this size.  Used to prevent queue build up with really large values")

MCROUTER_OPTION_INTEGER(
  size_t, fibers_max_pool_size, 1000,
  "fibers-max-pool-size", no_short,
  "Maximum number of preallocated free fibers to keep around")

MCROUTER_OPTION_INTEGER(
  size_t, fibers_stack_size, 24 * 1024,
  "fibers-stack-size", no_short,
  "Size of stack in bytes to allocate per fiber."
  " 0 means use fibers library default.")

MCROUTER_OPTION_INTEGER(
  size_t, fibers_record_stack_size_every, 100000,
  "fibers-record-stack-size-every", no_short,
  "Record exact amount of fibers stacks used for every N fiber. "
  "0 disables stack recording.")

MCROUTER_OPTION_TOGGLE(
  fibers_use_guard_pages, true,
  "disable-fibers-use-guard-pages", no_short,
  "If enabled, protect limited amount of fiber stacks with guard pages")

MCROUTER_OPTION_STRING(
  runtime_vars_file,
  MCROUTER_RUNTIME_VARS_DEFAULT,
  "runtime-vars-file", no_short,
  "Path to the runtime variables file.")

MCROUTER_OPTION_INTEGER(
  uint32_t, file_observer_poll_period_ms, 100,
  "file-observer-poll-period-ms", no_short,
  "How often to check inotify for updates on the tracked files.")

MCROUTER_OPTION_INTEGER(
  uint32_t, file_observer_sleep_before_update_ms, 1000,
  "file-observer-sleep-before-update-ms", no_short,
  "How long to sleep for after an update occured"
  " (a hack to avoid partial writes).")

MCROUTER_OPTION_INTEGER(
  uint32_t, fibers_pool_resize_period_ms, 60000,
  "fibers-pool-resize-period-ms", no_short,
  "Free unnecessary fibers in the fibers pool every"
  " fibers-pool-resize-period-ms milliseconds.  If value is 0, periodic"
  " resizing of the free pool is disabled.")

MCROUTER_OPTION_GROUP("Network")

MCROUTER_OPTION_INTEGER(
  int, keepalive_cnt, 0,
  "keepalive-count", 'K',
  "set TCP KEEPALIVE count, 0 to disable")

MCROUTER_OPTION_INTEGER(
  int, keepalive_interval_s, 60,
  "keepalive-interval", 'i',
  "set TCP KEEPALIVE interval parameter in seconds")

MCROUTER_OPTION_INTEGER(
  int, keepalive_idle_s, 300,
  "keepalive-idle", 'I',
  "set TCP KEEPALIVE idle parameter in seconds")

MCROUTER_OPTION_INTEGER(
  unsigned int, reset_inactive_connection_interval, 60000,
  "reset-inactive-connection-interval", no_short,
  "Will close open connections without any activity after at most 2 * interval"
  " ms. If value is 0, connections won't be closed.")

MCROUTER_OPTION_INTEGER(
  int, tcp_rto_min, -1,
  "tcp-rto-min", no_short,
  "adjust the minimum TCP retransmit timeout (ms) to memcached")

MCROUTER_OPTION_INTEGER(
  uint64_t, target_max_inflight_requests, 0,
  "target-max-inflight-requests", no_short,
  "Maximum inflight requests allowed per target per thread"
  " (0 means no throttling)")

MCROUTER_OPTION_INTEGER(
  uint64_t, target_max_pending_requests, 100000,
  "target-max-pending-requests", no_short,
  "Only active if target-max-inflight-requests is nonzero."
  " Hard limit on the number of requests allowed in the queue"
  " per target per thread.  Requests that would exceed this limit are dropped"
  " immediately.")

MCROUTER_OPTION_INTEGER(
  size_t, target_max_shadow_requests, 1000,
  "target-max-shadow-requests", no_short,
  "Hard limit on the number of shadow requests allowed in the queue"
  " per target per thread.  Requests that would exceed this limit are dropped"
  " immediately.")

MCROUTER_OPTION_TOGGLE(
  no_network, false, "no-network", no_short,
  "Debug only. Return random generated replies, do not use network.")

MCROUTER_OPTION_INTEGER(
  size_t, proxy_max_inflight_requests, 0,
  "proxy-max-inflight-requests", no_short,
  "If non-zero, sets the limit on maximum incoming requests that will be routed"
  " in parallel by each proxy thread.  Requests over limit will be queued up"
  " until the number of inflight requests drops.")

MCROUTER_OPTION_INTEGER(
  size_t, proxy_max_throttled_requests, 0,
  "proxy-max-throttled-requests", no_short,
  "Only active if proxy-max-inflight-requests is non-zero. "
  "Hard limit on the number of requests to queue per proxy after "
  "there are already proxy-max-inflight-requests requests in flight for the "
  "proxy. Further requests will be rejected with an error immediately. 0 means "
  "disabled.")

MCROUTER_OPTION_STRING(
  pem_cert_path, "",
  "pem-cert-path", no_short,
  "Path of pem-style certificate for ssl")

MCROUTER_OPTION_STRING(
  pem_key_path, "",
  "pem-key-path", no_short,
  "Path of pem-style key for ssl")

MCROUTER_OPTION_STRING(
  pem_ca_path, "",
  "pem-ca-path", no_short,
  "Path of pem-style CA cert for ssl")

MCROUTER_OPTION_TOGGLE(
  enable_qos, false,
  "enable-qos", no_short,
  "If enabled, sets the DSCP field in IP header according "
  "to the specified qos class.")

MCROUTER_OPTION_INTEGER(
  unsigned int, default_qos_class, 0,
  "default-qos-class", no_short,
  "Default qos class to use if qos is enabled and the class is not specified "
  "in pool/server config. The classes go from 0 (lowest priority) to "
  "4 (highest priority) and act on the hightest-order bits of DSCP.")

MCROUTER_OPTION_INTEGER(
  unsigned int, default_qos_path, 0,
  "default-qos-path", no_short,
  "Default qos path priority class to use if qos is enabled and it is not "
  "specified in the pool/server config. The path priority classes go from "
  "0 (lowest priority) to 3 (highest priority) and act on the lowest-order "
  "bits of DSCP.")

MCROUTER_OPTION_TOGGLE(
  ssl_connection_cache, false,
  "ssl-connection-cache", no_short,
  "If enabled, limited number of SSL sessions will be cached")

MCROUTER_OPTION_TOGGLE(
  enable_compression, false,
  "enable-compression", no_short,
  "If enabled, mcrouter replies will be compressed according to the "
  "compression algorithms/dictionaries supported by the client. Only "
  "compresses caret protocol replies.")

MCROUTER_OPTION_GROUP("Routing configuration")

MCROUTER_OPTION_TOGGLE(
  constantly_reload_configs, false,
  "constantly-reload-configs", no_short,
  "")

MCROUTER_OPTION_TOGGLE(
  disable_reload_configs, false,
  "disable-reload-configs", no_short,
  "")

MCROUTER_OPTION_STRING(
  config, "",
  "config", no_short,
  "Configuration to use. The provided string must be of one of two forms:"
  " file:<path-to-config-file> OR <JSON-config-string>. If provided,"
  " this option supersedes the deprecated config-file and config-str options.")

MCROUTER_OPTION_STRING(
  config_file, "",
  "config-file", 'f',
  "DEPRECATED. Load configuration from file. This option has no effect if"
  " --config option is used.")

MCROUTER_OPTION_STRING(
  config_str, "",
  "config-str", no_short,
  "DEPRECATED. Configuration string provided as a command line argument."
  " This option has no effect if --config option is used.")

MCROUTER_OPTION(
  facebook::memcache::mcrouter::RoutingPrefix, default_route, "/././",
  "route-prefix", 'R',
  "default routing prefix (ex. /oregon/prn1c16/)", routing_prefix)

MCROUTER_OPTION_TOGGLE(
  miss_on_get_errors, true,
  "disable-miss-on-get-errors", no_short,
  "Disable reporting get errors as misses")

MCROUTER_OPTION_TOGGLE(
  group_remote_errors, false,
  "group-remote-errors", no_short,
  "Groups all remote (i.e. non-local) errors together, returning a single "
  "result for all of them: mc_res_remote_error")

MCROUTER_OPTION_TOGGLE(
  send_invalid_route_to_default, false,
  "send-invalid-route-to-default", no_short,
  "Send request to default route if routing prefix is not present in config")

MCROUTER_OPTION_TOGGLE(
  enable_flush_cmd, false,
  "enable-flush-cmd", no_short,
  "Enable flush_all command")

MCROUTER_OPTION_INTEGER(
  int, reconfiguration_delay_ms, 1000,
  "reconfiguration-delay-ms", no_short,
  "Delay between config files change and mcrouter reconfiguration.")

MCROUTER_OPTION_STRING_MAP(
  config_params, "config-params", no_short,
  "Params for config preprocessor in format 'name1:value1,name2:value2'. "
  "All values will be passed as strings.")

MCROUTER_OPTION_GROUP("TKO probes")

MCROUTER_OPTION_TOGGLE(
  disable_tko_tracking, false,
  "disable-tko-tracking", no_short,
  "Disable TKO tracker (marking a host down for fast failover after"
  " a number of failures, and sending probes to check if the server"
  " came back up).")

MCROUTER_OPTION_INTEGER(
  int, probe_delay_initial_ms, 10000,
  "probe-timeout-initial", 'r',
  "TKO probe retry initial timeout in ms")

MCROUTER_OPTION_INTEGER(
  int, probe_delay_max_ms, 60000,
  "probe-timeout-max", no_short,
  "TKO probe retry max timeout in ms")

MCROUTER_OPTION_INTEGER(
  int, failures_until_tko, 3,
  "timeouts-until-tko", no_short,
  "Mark as TKO after this many failures")

MCROUTER_OPTION_INTEGER(
  size_t, maximum_soft_tkos, 40,
  "maximum-soft-tkos", no_short,
  "The maximum number of machines we can mark TKO if they don't have a hard"
  " failure.")

MCROUTER_OPTION_TOGGLE(
  allow_only_gets, false,
  "allow-only-gets", no_short,
  "Testing only. Allow only get-like operations: get, metaget, lease get. "
  "For any other operation return a default reply (not stored/not found).")


MCROUTER_OPTION_GROUP("Timeouts")

MCROUTER_OPTION_INTEGER(
  unsigned int, server_timeout_ms, 1000,
  "server-timeout", 't',
  "Timeout for talking to destination servers (e.g. memcached), "
  "in milliseconds. Must be greater than 0.")

MCROUTER_OPTION_INTEGER(
  unsigned int, cross_region_timeout_ms, 0,
  "cross-region-timeout-ms", no_short,
  "Timeouts for talking to cross region pool. "
  "If specified (non 0) takes precedence over every other timeout.")

MCROUTER_OPTION_INTEGER(
  unsigned int, cross_cluster_timeout_ms, 0,
  "cross-cluster-timeout-ms", no_short,
  "Timeouts for talking to pools within same region but different cluster. "
  "If specified (non 0) takes precedence over every other timeout.")

MCROUTER_OPTION_INTEGER(
  unsigned int, within_cluster_timeout_ms, 0,
  "within-cluster-timeout-ms", no_short,
  "Timeouts for talking to pools within same cluster. "
  "If specified (non 0) takes precedence over every other timeout.")

MCROUTER_OPTION_INTEGER(
  unsigned int, waiting_request_timeout_ms, 0,
  "waiting-request-timeout-ms", no_short,
  "Maximum time in ms that a new request can wait in the queue before being"
  " discarded. Enabled only if value is non-zero and"
  " if proxy-max-throttled-requests is enabled.")

MCROUTER_OPTION_GROUP("Custom Memory Allocation")

MCROUTER_OPTION_TOGGLE(
  jemalloc_nodump_buffers, false,
  "jemalloc-nodump-buffers", no_short,
  "Use the JemallocNodumpAllocator custom allocator. "
  "As the name suggests the memory allocated by this allocator will not be"
  " part of any core dump. This is achieved by setting MADV_DONTDUMP on"
  " explicitly created jemalloc arenas. The default value is false.")


MCROUTER_OPTION_GROUP("Logging")

MCROUTER_OPTION_STRING(
  stats_root, MCROUTER_STATS_ROOT_DEFAULT,
  "stats-root", no_short,
  "Root directory for stats files")

MCROUTER_OPTION_STRING(
  debug_fifo_root, DEBUG_FIFO_ROOT_DEFAULT,
  "debug-fifo-root", no_short,
  "Root directory for debug fifos. If empty, debug fifos are disabled.")

MCROUTER_OPTION_INTEGER(
  unsigned int, stats_logging_interval, 10000,
  "stats-logging-interval", no_short,
  "Time in ms between stats reports, or 0 for no logging")

MCROUTER_OPTION_INTEGER(
  unsigned int, logging_rtt_outlier_threshold_us, 0,
  "logging-rtt-outlier-threshold-us", no_short,
  "surpassing this threshold rtt time means we will log it as an outlier. "
  "0 (the default) means that we will do no logging of outliers.")

MCROUTER_OPTION_INTEGER(
  unsigned int, stats_async_queue_length, 50,
  "stats-async-queue-length", no_short,
  "Asynchronous queue size for logging.")

MCROUTER_OPTION_TOGGLE(
  enable_failure_logging, true,
  "disable-failure-logging", no_short,
  "Disable failure logging.")

MCROUTER_OPTION_TOGGLE(
  cpu_cycles, false,
  "cpu-cycles", no_short,
  "Enables CPU cycles counting for performance measurement.")

MCROUTER_OPTION_TOGGLE(
  test_mode, false,
  "test-mode", no_short,
  "Starts mcrouter in test mode - with logging disabled.")

MCROUTER_OPTION_TOGGLE(
  enable_logging_route, false,
  "enable-logging-route", no_short,
  "Log every request via LoggingRoute.")

MCROUTER_OPTION_INTEGER(
  uint64_t,
  collect_rxmit_stats_every_hz,
  0,
  "collect-rxmit-stats-every-hz",
  no_short,
  "Will calculate retransmits per kB after every set cycles."
  " If value is 0, calculation won't be done.")

MCROUTER_OPTION_INTEGER(
  uint64_t,
  min_rxmit_reconnect_threshold,
  0,
  "min-rxmit-reconnect-threshold",
  no_short,
  "If value is non-zero, mcrouter will reconnect to a target after hitting"
  " min-rxmit-reconnect-threshold retransmits per kb for the first time."
  " Subsequently, the reconnection threshold for the same target server is"
  " dynamically adjusted, always remaining at least"
  " min-rxmit-reconnect-threshold rxmits/kb. If value is 0,"
  " this feature is disabled.")

MCROUTER_OPTION_INTEGER(
  uint64_t,
  max_rxmit_reconnect_threshold,
  0,
  "max-rxmit-reconnect-threshold",
  no_short,
  "Has no effect if min-rxmit-reconnect-threshold is 0."
  " If max-rxmit-reconnect-threshold is also non-zero, the dynamic reconnection"
  " threshold is always at most max-rxmit-reconnect-threshold rxmits/kb."
  " If max-rxmit-reconnect-threshold is 0, the dynamic threshold is unbounded.")

MCROUTER_OPTION_INTEGER(
  int, asynclog_port_override, 0, no_long, no_short,
  "If non-zero use this port while logging to async log")

#ifdef ADDITIONAL_OPTIONS_FILE
#include ADDITIONAL_OPTIONS_FILE
#endif

#undef no_short
#undef no_long
#undef MCROUTER_OPTION_GROUP