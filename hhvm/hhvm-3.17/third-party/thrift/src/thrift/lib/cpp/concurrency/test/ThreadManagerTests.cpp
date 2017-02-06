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

#include <numa.h>

#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

#include <folly/Synchronized.h>
#include <folly/portability/SysResource.h>
#include <folly/portability/SysTime.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/concurrency/FunctionRunner.h>
#include <thrift/lib/cpp/concurrency/Monitor.h>
#include <thrift/lib/cpp/concurrency/NumaThreadManager.h>
#include <thrift/lib/cpp/concurrency/PosixThreadFactory.h>
#include <thrift/lib/cpp/concurrency/ThreadManager.h>
#include <thrift/lib/cpp/concurrency/Util.h>
#include <wangle/concurrent/Codel.h>

using namespace apache::thrift::concurrency;

DECLARE_bool(thrift_numa_enabled);

class ThreadManagerTest : public testing::Test {
 public:
  ~ThreadManagerTest() override {
    ThreadManager::setObserver(nullptr);
  }
 private:
  google::FlagSaver flagsaver_;
};

// Loops until x==y for up to timeout ms.
// The end result is the same as of {EXPECT,ASSERT}_EQ(x,y)
// (depending on OP) if x!=y after the timeout passes
#define X_EQUAL_SPECIFIC_TIMEOUT(OP, timeout, x, y) do { \
    using std::chrono::steady_clock; \
    using std::chrono::milliseconds;  \
    auto end = steady_clock::now() + milliseconds(timeout);  \
    while ((x) != (y) && steady_clock::now() < end)  {} \
    OP##_EQ(x, y); \
  } while (0)

#define CHECK_EQUAL_SPECIFIC_TIMEOUT(timeout, x, y) \
  X_EQUAL_SPECIFIC_TIMEOUT(EXPECT, timeout, x, y)
#define REQUIRE_EQUAL_SPECIFIC_TIMEOUT(timeout, x, y) \
  X_EQUAL_SPECIFIC_TIMEOUT(ASSERT, timeout, x, y)

// A default timeout of 1 sec should be long enough for other threads to
// stabilize the values of x and y, and short enough to catch real errors
// when x is not going to be equal to y anytime soon
#define CHECK_EQUAL_TIMEOUT(x, y) CHECK_EQUAL_SPECIFIC_TIMEOUT(1000, x, y)
#define REQUIRE_EQUAL_TIMEOUT(x, y) REQUIRE_EQUAL_SPECIFIC_TIMEOUT(1000, x, y)

class LoadTask: public Runnable {
 public:
  LoadTask(Monitor* monitor, size_t* count, int64_t timeout)
    : monitor_(monitor),
      count_(count),
      timeout_(timeout),
      startTime_(0),
      endTime_(0) {}

  void run() override {
    startTime_ = Util::currentTime();
    usleep(timeout_ * Util::US_PER_MS);
    endTime_ = Util::currentTime();

    {
      Synchronized s(*monitor_);

      (*count_)--;
      if (*count_ == 0) {
        monitor_->notify();
      }
    }
  }

  Monitor* monitor_;
  size_t* count_;
  int64_t timeout_;
  int64_t startTime_;
  int64_t endTime_;
};

/**
 * Dispatch count tasks, each of which blocks for timeout milliseconds then
 * completes. Verify that all tasks completed and that thread manager cleans
 * up properly on delete.
 */
static void loadTest(size_t numTasks, int64_t timeout, size_t numWorkers) {
  Monitor monitor;
  size_t tasksLeft = numTasks;

  auto threadManager =
    ThreadManager::newSimpleThreadManager(numWorkers, 0, true);
  auto threadFactory = std::make_shared<PosixThreadFactory>();
  threadManager->threadFactory(threadFactory);
  threadManager->start();

  std::set<std::shared_ptr<LoadTask>> tasks;
  for (size_t n = 0; n < numTasks; n++) {
    tasks.insert(std::make_shared<LoadTask>(&monitor, &tasksLeft, timeout));
  }

  int64_t startTime = Util::currentTime();
  for (const auto& task : tasks) {
    threadManager->add(task);
  }

  int64_t tasksStartedTime = Util::currentTime();

  {
    Synchronized s(monitor);
    while (tasksLeft > 0) {
      monitor.wait();
    }
  }
  int64_t endTime = Util::currentTime();

  int64_t firstTime = std::numeric_limits<int64_t>::max();
  int64_t lastTime = 0;
  double averageTime = 0;
  int64_t minTime = std::numeric_limits<int64_t>::max();
  int64_t maxTime = 0;

  for (const auto& task : tasks) {
    EXPECT_GT(task->startTime_, 0);
    EXPECT_GT(task->endTime_, 0);

    int64_t delta = task->endTime_ - task->startTime_;
    assert(delta > 0);

    firstTime = std::min(firstTime, task->startTime_);
    lastTime = std::max(lastTime, task->endTime_);
    minTime = std::min(minTime, delta);
    maxTime = std::max(maxTime, delta);

    averageTime += delta;
  }
  averageTime /= numTasks;

  LOG(INFO) << "first start: " << firstTime << "ms "
            << "last end: " << lastTime << "ms "
            << "min: " << minTime << "ms "
            << "max: " << maxTime << "ms "
            << "average: " << averageTime << "ms";

  double idealTime = ((numTasks + (numWorkers - 1)) / numWorkers) * timeout;
  double actualTime = endTime - startTime;
  double taskStartTime = tasksStartedTime - startTime;

  double overheadPct = (actualTime - idealTime) / idealTime;
  if (overheadPct < 0) {
    overheadPct*= -1.0;
  }

  LOG(INFO) << "ideal time: " << idealTime << "ms "
            << "actual time: "<< actualTime << "ms "
            << "task startup time: " << taskStartTime << "ms "
            << "overhead: " << overheadPct * 100.0 << "%";

  // Fail if the test took 10% more time than the ideal time
  EXPECT_LT(overheadPct, 0.10);

  // Get the task stats
  int64_t waitTimeUs;
  int64_t runTimeUs;
  threadManager->getStats(waitTimeUs, runTimeUs, numTasks * 2);

  // Compute the best possible average wait time
  int64_t fullIterations = numTasks / numWorkers;
  int64_t tasksOnLastIteration = numTasks % numWorkers;
  int64_t expectedTotalWaitTimeMs =
    numWorkers * ((fullIterations * (fullIterations - 1)) / 2) * timeout +
    tasksOnLastIteration * fullIterations * timeout;
  int64_t idealAvgWaitUs =
    (expectedTotalWaitTimeMs * Util::US_PER_MS) / numTasks;

  LOG(INFO) << "avg wait time: " << waitTimeUs << "us "
            << "avg run time: " << runTimeUs << "us "
            << "ideal wait time: " << idealAvgWaitUs << "us";

  // Verify that the average run time was more than the timeout, but not
  // more than 10% over.
  EXPECT_GE(runTimeUs, timeout * Util::US_PER_MS);
  EXPECT_LT(runTimeUs, timeout * Util::US_PER_MS * 1.10);
  // Verify that the average wait time was within 10% of the ideal wait time.
  // The calculation for ideal average wait time assumes all tasks were started
  // instantaneously, in reality, starting 1000 tasks takes some non-zero amount
  // of time, so later tasks will actually end up waiting for *less* than the
  // ideal wait time. Account for this by accepting an actual avg wait time that
  // is less than ideal avg wait time by up to the time it took to start all the
  // tasks.
  EXPECT_GE(waitTimeUs, idealAvgWaitUs - taskStartTime * Util::US_PER_MS);
  EXPECT_LT(waitTimeUs, idealAvgWaitUs * 1.10);
}

TEST_F(ThreadManagerTest, LoadTest) {
  size_t numTasks = 10000;
  int64_t timeout = 50;
  size_t numWorkers = 100;
  loadTest(numTasks, timeout, numWorkers);
}

class BlockTask: public Runnable {
 public:
  BlockTask(Monitor* monitor, Monitor* bmonitor, bool* blocked, size_t* count)
    : monitor_(monitor),
      bmonitor_(bmonitor),
      blocked_(blocked),
      count_(count),
      started_(false) {}

  void run() override {
    started_ = true;
    {
      Synchronized s(*bmonitor_);
      while (*blocked_) {
        bmonitor_->wait();
      }
    }

    {
      Synchronized s(*monitor_);
      (*count_)--;
      if (*count_ == 0) {
        monitor_->notify();
      }
    }
  }

  Monitor* monitor_;
  Monitor* bmonitor_;
  bool* blocked_;
  size_t* count_;
  bool started_;
};

/**
 * Block test.
 * Create pendingTaskCountMax tasks.  Verify that we block adding the
 * pendingTaskCountMax + 1th task.  Verify that we unblock when a task
 * completes
 */
static void blockTest(int64_t /*timeout*/, size_t numWorkers) {
  size_t pendingTaskMaxCount = numWorkers;

  auto threadManager =
    ThreadManager::newSimpleThreadManager(numWorkers, pendingTaskMaxCount);
  auto threadFactory = std::make_shared<PosixThreadFactory>();
  threadManager->threadFactory(threadFactory);
  threadManager->start();

  Monitor monitor;
  Monitor bmonitor;

  // Add an initial set of tasks, 1 task per worker
  bool blocked1 = true;
  size_t tasksCount1 = numWorkers;
  std::set<std::shared_ptr<BlockTask>> tasks;
  for (size_t ix = 0; ix < numWorkers; ix++) {
    auto task = std::make_shared<BlockTask>(
        &monitor, &bmonitor, &blocked1, &tasksCount1);
    tasks.insert(task);
    threadManager->add(task);
  }
  REQUIRE_EQUAL_TIMEOUT(threadManager->totalTaskCount(), numWorkers);

  // Add a second set of tasks.
  // All of these will end up pending since the first set of tasks
  // are using up all of the worker threads and are still blocked
  bool blocked2 = true;
  size_t tasksCount2 = pendingTaskMaxCount;
  for (size_t ix = 0; ix < pendingTaskMaxCount; ix++) {
    auto task = std::make_shared<BlockTask>(
        &monitor, &bmonitor, &blocked2, &tasksCount2);
    tasks.insert(task);
    threadManager->add(task);
  }

  REQUIRE_EQUAL_TIMEOUT(threadManager->totalTaskCount(),
                      numWorkers + pendingTaskMaxCount);
  REQUIRE_EQUAL_TIMEOUT(threadManager->pendingTaskCountMax(),
                      pendingTaskMaxCount);

  // Attempt to add one more task.
  // Since the pending task count is full, this should fail
  bool blocked3 = true;
  size_t tasksCount3 = 1;
  auto extraTask = std::make_shared<BlockTask>(
      &monitor, &bmonitor, &blocked3, &tasksCount3);
  ASSERT_THROW(threadManager->add(extraTask, 1), TimedOutException);

  ASSERT_THROW(threadManager->add(extraTask, -1), TooManyPendingTasksException);

  // Unblock the first set of tasks
  {
    Synchronized s(bmonitor);
    blocked1 = false;
    bmonitor.notifyAll();
  }
  // Wait for the first set of tasks to all finish
  {
    Synchronized s(monitor);
    while (tasksCount1 != 0) {
      monitor.wait();
    }
  }

  // We should be able to add the extra task now
  try {
    threadManager->add(extraTask, 1);
  } catch (const TimedOutException& e) {
    FAIL() << "Unexpected timeout adding task";
  } catch (const TooManyPendingTasksException& e) {
    FAIL() << "Unexpected failure adding task";
  }

  // Unblock the second set of tasks
  {
    Synchronized s(bmonitor);
    blocked2 = false;
    bmonitor.notifyAll();
  }
  {
    Synchronized s(monitor);
    while (tasksCount2 != 0) {
      monitor.wait();
    }
  }

  // Unblock the extra task
  {
    Synchronized s(bmonitor);
    blocked3 = false;
    bmonitor.notifyAll();
  }
  {
    Synchronized s(monitor);
    while (tasksCount3 != 0) {
      monitor.wait();
    }
  }

  CHECK_EQUAL_TIMEOUT(threadManager->totalTaskCount(), 0);
}

TEST_F(ThreadManagerTest, BlockTest) {
  int64_t timeout = 50;
  size_t numWorkers = 100;
  blockTest(timeout, numWorkers);
}

static void expireTestCallback(std::shared_ptr<Runnable>,
                               Monitor* monitor,
                               size_t* count) {
  Synchronized s(*monitor);
  --(*count);
  if (*count == 0) {
    monitor->notify();
  }
}

static void expireTest(int64_t numWorkers, int64_t expirationTimeMs) {
  int64_t maxPendingTasks = numWorkers;
  size_t activeTasks = numWorkers + maxPendingTasks;
  Monitor monitor;

  auto threadManager =
    ThreadManager::newSimpleThreadManager(numWorkers, maxPendingTasks);
  auto threadFactory = std::make_shared<PosixThreadFactory>();
  threadManager->threadFactory(threadFactory);
  threadManager->setExpireCallback(
      std::bind(expireTestCallback, std::placeholders::_1,
                     &monitor, &activeTasks));
  threadManager->start();

  // Add numWorkers + maxPendingTasks to fill up the ThreadManager's task queue
  std::vector<std::shared_ptr<BlockTask>> tasks;
  tasks.reserve(activeTasks);

  Monitor bmonitor;
  bool blocked = true;
  for (int64_t n = 0; n < numWorkers + maxPendingTasks; ++n) {
    auto task = std::make_shared<BlockTask>(
        &monitor, &bmonitor, &blocked, &activeTasks);
    tasks.push_back(task);
    threadManager->add(task, 0, expirationTimeMs);
  }

  // Sleep for more than the expiration time
  usleep(expirationTimeMs * Util::US_PER_MS * 1.10);

  // Unblock the tasks
  {
    Synchronized s(bmonitor);
    blocked = false;
    bmonitor.notifyAll();
  }
  // Wait for all tasks to complete or expire
  {
    Synchronized s(monitor);
    while (activeTasks != 0) {
      monitor.wait();
    }
  }

  // The first numWorkers tasks should have completed,
  // the remaining ones should have expired without running
  size_t index = 0;
  for (const auto& task : tasks) {
    if (index < numWorkers) {
      EXPECT_TRUE(tasks[index]->started_);
    } else {
      EXPECT_TRUE(!tasks[index]->started_);
    }
    ++index;
  }
}

TEST_F(ThreadManagerTest, ExpireTest) {
  int64_t numWorkers = 100;
  int64_t expireTimeMs = 50;
  expireTest(numWorkers, expireTimeMs);
}

class AddRemoveTask : public Runnable,
                      public std::enable_shared_from_this<AddRemoveTask> {
 public:
  AddRemoveTask(uint32_t timeoutUs,
                const std::shared_ptr<ThreadManager>& manager,
                Monitor* monitor,
                int64_t* count,
                int64_t* objectCount)
    : timeoutUs_(timeoutUs),
      manager_(manager),
      monitor_(monitor),
      count_(count),
      objectCount_(objectCount) {
    Synchronized s(monitor_);
    ++*objectCount_;
  }

  ~AddRemoveTask() override {
    Synchronized s(monitor_);
    --*objectCount_;
  }

  void run() override {
    usleep(timeoutUs_);

    {
      Synchronized s(monitor_);

      if (*count_ <= 0) {
        // The task count already dropped to 0.
        // We add more tasks than count_, so some of them may still be running
        // when count_ drops to 0.
        return;
      }

      --*count_;
      if (*count_ == 0) {
        monitor_->notifyAll();
        return;
      }
    }

    // Add ourself to the task queue again
    manager_->add(shared_from_this());
  }

 private:
  int32_t timeoutUs_;
  std::shared_ptr<ThreadManager> manager_;
  Monitor* monitor_;
  int64_t* count_;
  int64_t* objectCount_;
};

class WorkerCountChanger : public Runnable {
 public:
  WorkerCountChanger(const std::shared_ptr<ThreadManager>& manager,
                     Monitor* monitor,
                     int64_t *count,
                     int64_t* addAndRemoveCount)
    : manager_(manager),
      monitor_(monitor),
      count_(count),
      addAndRemoveCount_(addAndRemoveCount) {}

  void run() override {
    // Continue adding and removing threads until the tasks are all done
    while (true) {
      {
        Synchronized s(monitor_);
        if (*count_ == 0) {
          return;
        }
        ++*addAndRemoveCount_;
      }
      addAndRemove();
    }
  }

  void addAndRemove() {
    // Add a random number of workers
    std::uniform_int_distribution<> workerDist(1, 10);
    uint32_t workersToAdd = workerDist(rng_);
    manager_->addWorker(workersToAdd);

    std::uniform_int_distribution<> taskDist(1, 50);
    uint32_t tasksToAdd = taskDist(rng_);

    // Sleep for a random amount of time
    std::uniform_int_distribution<> sleepDist(1000, 5000);
    uint32_t sleepUs = sleepDist(rng_);
    usleep(sleepUs);

    // Remove the same number of workers we added
    manager_->removeWorker(workersToAdd);
  }

 private:
  std::mt19937 rng_;
  std::shared_ptr<ThreadManager> manager_;
  Monitor* monitor_;
  int64_t* count_;
  int64_t* addAndRemoveCount_;
};

// Run lots of tasks, while several threads are all changing
// the number of worker threads.
TEST_F(ThreadManagerTest, AddRemoveWorker) {
  // Number of tasks to run
  int64_t numTasks = 100000;
  // Minimum number of workers to keep at any point in time
  size_t minNumWorkers = 10;
  // Number of threads that will be adding and removing workers
  int64_t numAddRemoveWorkers = 30;
  // Number of tasks to run in parallel
  int64_t numParallelTasks = 200;

  auto threadManager = ThreadManager::newSimpleThreadManager(minNumWorkers);
  auto threadFactory = std::make_shared<PosixThreadFactory>();
  threadManager->threadFactory(threadFactory);
  threadManager->start();

  Monitor monitor;
  int64_t currentTaskObjects = 0;
  int64_t count = numTasks;
  int64_t addRemoveCount = 0;

  std::mt19937 rng;
  std::uniform_int_distribution<> taskTimeoutDist(1, 3000);
  for (int64_t n = 0; n < numParallelTasks; ++n) {
    int64_t taskTimeoutUs = taskTimeoutDist(rng);
    auto task = std::make_shared<AddRemoveTask>(
        taskTimeoutUs, threadManager, &monitor, &count,
        &currentTaskObjects);
    threadManager->add(task);
  }

  auto addRemoveFactory = std::make_shared<PosixThreadFactory>();
  addRemoveFactory->setDetached(false);
  std::deque<std::shared_ptr<Thread> > addRemoveThreads;
  for (int64_t n = 0; n < numAddRemoveWorkers; ++n) {
    auto worker = std::make_shared<WorkerCountChanger>(
          threadManager, &monitor, &count, &addRemoveCount);
    auto thread = addRemoveFactory->newThread(worker);
    addRemoveThreads.push_back(thread);
    thread->start();
  }

  while (!addRemoveThreads.empty()) {
    addRemoveThreads.front()->join();
    addRemoveThreads.pop_front();
  }

  LOG(INFO) << "add remove count: " << addRemoveCount;
  EXPECT_GT(addRemoveCount, 0);

  // Stop the ThreadManager, and ensure that all Task objects have been
  // destroyed.
  threadManager->stop();
  EXPECT_EQ(0, currentTaskObjects);
}

TEST_F(ThreadManagerTest, NeverStartedTest) {
  // Test destroying a ThreadManager that was never started.
  // This ensures that calling stop() on an unstarted ThreadManager works
  // properly.
  {
    auto threadManager = ThreadManager::newSimpleThreadManager(10);
  }

  // Destroy a ThreadManager that has a ThreadFactory but was never started.
  {
    auto threadManager = ThreadManager::newSimpleThreadManager(10);
    auto threadFactory = std::make_shared<PosixThreadFactory>();
    threadManager->threadFactory(threadFactory);
  }
}

TEST_F(ThreadManagerTest, OnlyStartedTest) {
  // Destroy a ThreadManager that has a ThreadFactory and was started.
  for (int i = 0; i < 1000; ++i) {
    auto threadManager = ThreadManager::newSimpleThreadManager(10);
    auto threadFactory = std::make_shared<PosixThreadFactory>();
    threadManager->threadFactory(threadFactory);
    threadManager->start();
  }
}

class TestObserver : public ThreadManager::Observer {
 public:
  TestObserver(int64_t timeout, const std::string& expectedName)
    : timesCalled(0)
    , timeout(timeout)
    , expectedName(expectedName) {}

  void preRun(folly::RequestContext*) override {}
  void postRun(
      folly::RequestContext*,
      const ThreadManager::RunStats& stats) override {
    EXPECT_EQ(expectedName, stats.threadPoolName);

    // Note: Technically could fail if system clock changes.
    EXPECT_GT((stats.workBegin - stats.queueBegin).count(), 0);
    EXPECT_GT((stats.workEnd - stats.workBegin).count(), 0);
    EXPECT_GT((stats.workEnd - stats.workBegin).count(), timeout - 1);
    ++timesCalled;
  }

  uint64_t timesCalled;
  int64_t timeout;
  std::string expectedName;
};

TEST_F(ThreadManagerTest, NumaThreadManagerTest) {
  google::FlagSaver saver;
  FLAGS_thrift_numa_enabled = true;

  if (numa_available() == -1) {
    LOG(ERROR) << "numa is unavailable, skipping NumaThreadManagerTest";
    return;
  }

  auto numa = folly::make_unique<NumaThreadManager>(2);
  bool failed = false;

  numa->setNamePrefix("foo");
  numa->start();

  folly::Synchronized<std::set<int>> nodes;

  auto data = RequestContext::get()->getContextData("numa");
  EXPECT_EQ(nullptr, data);

  auto checkFunc = FunctionRunner::create([&](){
      auto data = RequestContext::get()->getContextData(
        "numa");
      // Check that the request is not bound unless requested
      if (nullptr != data) {
        failed = true;
      }
      auto node = NumaThreadFactory::getNumaNode();
      SYNCHRONIZED(nodes) {
        nodes.insert(node);
      }

      numa->add(FunctionRunner::create([=,&failed]() {
            auto data = RequestContext::get()->getContextData(
              "numa");
            if (nullptr != data) {
              failed = true;
            }
            // Check that multiple calls stay on the same node
            auto node2 = NumaThreadFactory::getNumaNode();
            if (node != node2) {
              failed = true;
            }
          }),
        0,
        0,
        true,
        true);
    });

  for (int i = 0; i < 100; i++) {
    numa->add(checkFunc,
    0,
    0,
    true,
    true);
  }

  numa->join();
  EXPECT_EQ(numa_num_configured_nodes(), nodes->size());
  EXPECT_FALSE(failed);
}

class FailThread : public PthreadThread {
 public:
  FailThread(int policy, int priority, int stackSize, bool detached,
             std::shared_ptr<Runnable> runnable)
    : PthreadThread(policy, priority, stackSize, detached, runnable) {
  }

  void start() override { throw 2; }
};

class FailThreadFactory : public PosixThreadFactory {
 public:
  class FakeImpl : public Impl {
   public:
    FakeImpl(POLICY policy, PosixThreadFactory::PRIORITY priority,
             int stackSize, DetachState detached)
      : Impl(policy, priority, stackSize, detached) {
    }

    std::shared_ptr<Thread> newThread(const std::shared_ptr<Runnable>& runnable,
                                      DetachState detachState) const override {
      auto result = std::make_shared<FailThread>(
          toPthreadPolicy(policy_),
          toPthreadPriority(policy_, priority_), stackSize_,
          detachState == DETACHED, runnable);
      result->weakRef(result);
      runnable->thread(result);
      return result;
    }
  };

  explicit FailThreadFactory(POLICY /*policy*/=kDefaultPolicy,
                             PRIORITY /*priority*/=kDefaultPriority,
                             int /*stackSize*/=kDefaultStackSizeMB,
                             bool detached=true) {
   impl_ = std::make_shared<FailThreadFactory::FakeImpl>(
        kDefaultPolicy,
        kDefaultPriority,
        kDefaultStackSizeMB, detached ? DETACHED : ATTACHED);
  }
};

class DummyFailureClass {
 public:
  DummyFailureClass() {
    threadManager_ = ThreadManager::newSimpleThreadManager(20);
    threadManager_->setNamePrefix("foo");
    auto threadFactory = std::make_shared<FailThreadFactory>();
    threadManager_->threadFactory(threadFactory);
    threadManager_->start();
  }
 private:
  std::shared_ptr<ThreadManager> threadManager_;
};

TEST_F(ThreadManagerTest, ThreadStartFailureTest) {
  for (int i = 0; i < 10; i++) {
    EXPECT_THROW(DummyFailureClass(), int);
  }
}

TEST_F(ThreadManagerTest, NumaThreadManagerBind) {
  google::FlagSaver saver;
  FLAGS_thrift_numa_enabled = true;

  auto numa = folly::make_unique<NumaThreadManager>(2);
  numa->setNamePrefix("foo");
  numa->start();

  // Test binding the request.  Only works on threads started with
  // NumaThreadManager.
  numa->add(FunctionRunner::create([=](){
      // Try binding the numa node
      NumaThreadFactory::setNumaNode();
      auto node = NumaThreadFactory::getNumaNode();
      EXPECT_NE(-1, node);
      }));
  numa->join();
}

TEST_F(ThreadManagerTest, ObserverTest) {
  int64_t timeout = 1000;
  auto observer = std::make_shared<TestObserver>(1000, "foo");
  ThreadManager::setObserver(observer);

  Monitor monitor;
  size_t tasks = 1;

  auto threadManager = ThreadManager::newSimpleThreadManager(10);
  threadManager->setNamePrefix("foo");
  threadManager->threadFactory(std::make_shared<PosixThreadFactory>());
  threadManager->start();

  auto task = std::make_shared<LoadTask>(&monitor, &tasks, 1000);
  threadManager->add(task);
  threadManager->join();
  EXPECT_EQ(1, observer->timesCalled);
}

TEST_F(ThreadManagerTest, ObserverAssignedAfterStart) {
  class MyTask : public Runnable {
   public:
    void run() override {}
  };
  class MyObserver : public ThreadManager::Observer {
   public:
    MyObserver(std::string name, std::shared_ptr<std::string> tgt) :
      name_(std::move(name)), tgt_(std::move(tgt)) {}
    void preRun(folly::RequestContext*) override {}
    void postRun(
        folly::RequestContext*,
        const ThreadManager::RunStats&) override {
      *tgt_ = name_;
    }
   private:
    std::string name_;
    std::shared_ptr<std::string> tgt_;
  };

  // start a tm
  auto tm = ThreadManager::newSimpleThreadManager(1);
  tm->setNamePrefix("foo");
  tm->threadFactory(std::make_shared<PosixThreadFactory>());
  tm->start();
  // set the observer w/ observable side-effect
  auto tgt = std::make_shared<std::string>();
  ThreadManager::setObserver(std::make_shared<MyObserver>("bar", tgt));
  // add a task - observable side-effect should trigger
  tm->add(std::make_shared<MyTask>());
  tm->join();
  // confirm the side-effect
  EXPECT_EQ("bar", *tgt);
}

TEST_F(ThreadManagerTest, PosixThreadFactoryPriority) {
  auto getNiceValue = [](PosixThreadFactory::PRIORITY prio) -> int {
    PosixThreadFactory factory(PosixThreadFactory::OTHER, prio);
    factory.setDetached(false);
    int result = 0;
    auto t = factory.newThread(FunctionRunner::create([&] {
      result = getpriority(PRIO_PROCESS, 0);
    }));
    t->start();
    t->join();
    return result;
  };

  // NOTE: Test may not have permission to raise priority,
  // so use prio <= NORMAL.
  EXPECT_EQ(0, getNiceValue(PosixThreadFactory::NORMAL));
  EXPECT_LT(0, getNiceValue(PosixThreadFactory::LOW));
  std::thread([&] {
    for (int i = 0; i < 20; ++i) {
      if (setpriority(PRIO_PROCESS, 0, i) != 0) {
        PLOG(WARNING) << "failed setpriority(" << i << ")";
        continue;
      }
      EXPECT_EQ(i, getNiceValue(PosixThreadFactory::INHERITED));
    }
  }).join();
}

TEST_F(ThreadManagerTest, PriorityThreadManagerWorkerCount) {
  auto threadManager = PriorityThreadManager::newPriorityThreadManager({{
      1 /*HIGH_IMPORTANT*/,
      2 /*HIGH*/,
      3 /*IMPORTANT*/,
      4 /*NORMAL*/,
      5 /*BEST_EFFORT*/
  }});
  threadManager->start();

  EXPECT_EQ(1, threadManager->workerCount(PRIORITY::HIGH_IMPORTANT));
  EXPECT_EQ(2, threadManager->workerCount(PRIORITY::HIGH));
  EXPECT_EQ(3, threadManager->workerCount(PRIORITY::IMPORTANT));
  EXPECT_EQ(4, threadManager->workerCount(PRIORITY::NORMAL));
  EXPECT_EQ(5, threadManager->workerCount(PRIORITY::BEST_EFFORT));

  threadManager->addWorker(PRIORITY::HIGH_IMPORTANT, 1);
  threadManager->addWorker(PRIORITY::HIGH, 1);
  threadManager->addWorker(PRIORITY::IMPORTANT, 1);
  threadManager->addWorker(PRIORITY::NORMAL, 1);
  threadManager->addWorker(PRIORITY::BEST_EFFORT, 1);

  EXPECT_EQ(2, threadManager->workerCount(PRIORITY::HIGH_IMPORTANT));
  EXPECT_EQ(3, threadManager->workerCount(PRIORITY::HIGH));
  EXPECT_EQ(4, threadManager->workerCount(PRIORITY::IMPORTANT));
  EXPECT_EQ(5, threadManager->workerCount(PRIORITY::NORMAL));
  EXPECT_EQ(6, threadManager->workerCount(PRIORITY::BEST_EFFORT));

  threadManager->removeWorker(PRIORITY::HIGH_IMPORTANT, 1);
  threadManager->removeWorker(PRIORITY::HIGH, 1);
  threadManager->removeWorker(PRIORITY::IMPORTANT, 1);
  threadManager->removeWorker(PRIORITY::NORMAL, 1);
  threadManager->removeWorker(PRIORITY::BEST_EFFORT, 1);

  EXPECT_EQ(1, threadManager->workerCount(PRIORITY::HIGH_IMPORTANT));
  EXPECT_EQ(2, threadManager->workerCount(PRIORITY::HIGH));
  EXPECT_EQ(3, threadManager->workerCount(PRIORITY::IMPORTANT));
  EXPECT_EQ(4, threadManager->workerCount(PRIORITY::NORMAL));
  EXPECT_EQ(5, threadManager->workerCount(PRIORITY::BEST_EFFORT));
}
