#pragma once

namespace wangle {
/**
 * A service filter that expires the self service after a certain
 * amount of idle time, or after a maximum amount of time total.
 * Idle timeout is cancelled when any requests are outstanding.
 */

template <typename Req, typename Resp = Req>
class ExpiringFilter : public ServiceFilter<Req, Resp> {
 public:
  explicit ExpiringFilter(std::shared_ptr<Service<Req, Resp>> service,
                 std::chrono::milliseconds idleTimeoutTime
                 = std::chrono::milliseconds(0),
                  std::chrono::milliseconds maxTime
                 = std::chrono::milliseconds(0),
                 folly::Timekeeper* timekeeper = nullptr)
  : ServiceFilter<Req, Resp>(service)
  , idleTimeoutTime_(idleTimeoutTime)
  , maxTime_(maxTime)
  , timekeeper_(timekeeper) {

    if (maxTime_ > std::chrono::milliseconds(0)) {
      maxTimeout_ = folly::futures::sleep(maxTime_, timekeeper_);
      maxTimeout_.then([this](){
        this->close();
      });
    }
    startIdleTimer();
  }

  ~ExpiringFilter() {
    if (!idleTimeout_.isReady()) {
      idleTimeout_.cancel();
    }
    if (!maxTimeout_.isReady()) {
      maxTimeout_.cancel();
    }
  }

  void startIdleTimer() {
    if (requests_ != 0) {
      return;
    }
    if (idleTimeoutTime_ > std::chrono::milliseconds(0)) {
      idleTimeout_ = folly::futures::sleep(idleTimeoutTime_, timekeeper_);
      idleTimeout_.then([this](){
        this->close();
      });
    }
  };

  virtual folly::Future<Resp> operator()(Req req) override {
    if (!idleTimeout_.isReady()) {
      idleTimeout_.cancel();
    }
    requests_++;
    return (*this->service_)(std::move(req)).ensure([this](){
        requests_--;
        startIdleTimer();
    });
  }

 private:
  folly::Future<folly::Unit> idleTimeout_;
  folly::Future<folly::Unit> maxTimeout_;
  std::chrono::milliseconds idleTimeoutTime_{0};
  std::chrono::milliseconds maxTime_{0};
  folly::Timekeeper* timekeeper_;
  uint32_t requests_{0};
};

} // namespace wangle
