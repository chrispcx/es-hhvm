/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cerrno>
#include <folly/DynamicConverter.h>
#include <folly/FileUtil.h>
#include <folly/json.h>
#include <folly/ScopeGuard.h>
#include <folly/portability/SysTime.h>
#include <functional>

namespace wangle {

template<typename K, typename V, typename MutexT>
LRUPersistentCache<K, V, MutexT>::LRUPersistentCache(
    const std::size_t cacheCapacity,
    const std::chrono::milliseconds& syncInterval,
    const int nSyncRetries,
    std::unique_ptr<CachePersistence<K, V>> persistence):
  cache_(cacheCapacity),
  stopSyncer_(false),
  syncInterval_(syncInterval),
  nSyncRetries_(nSyncRetries),
  persistence_(nullptr) {

  // load the cache. be silent if load fails, we just drop the cache
  // and start from scratch.
  if (persistence) {
    setPersistenceHelper(std::move(persistence), true);
  }
  // start the syncer thread. done at the end of construction so that the cache
  // is fully initialized before being passed to the syncer thread.
  syncer_ = std::thread(&LRUPersistentCache<K, V, MutexT>::syncThreadMain, this);
}

template<typename K, typename V, typename MutexT>
LRUPersistentCache<K, V, MutexT>::~LRUPersistentCache() {
  {
    // tell syncer to wake up and quit
    std::lock_guard<std::mutex> lock(stopSyncerMutex_);

    stopSyncer_ = true;
    stopSyncerCV_.notify_all();
  }

  syncer_.join();
}

template<typename K, typename V, typename MutexT>
bool LRUPersistentCache<K, V, MutexT>::hasPendingUpdates() {
  typename wangle::CacheLockGuard<MutexT>::Read readLock(persistenceLock_);
  if (!persistence_) {
    return false;
  }
  return cache_.hasChangedSince(persistence_->getLastPersistedVersion());
}

template<typename K, typename V, typename MutexT>
void* LRUPersistentCache<K, V, MutexT>::syncThreadMain(void* arg) {
  auto self = static_cast<LRUPersistentCache<K, V, MutexT>*>(arg);
  self->sync();
  return nullptr;
}

template<typename K, typename V, typename MutexT>
void LRUPersistentCache<K, V, MutexT>::sync() {
  // keep running as long the destructor signals to stop or
  // there are pending updates that are not synced yet
  std::unique_lock<std::mutex> stopSyncerLock(stopSyncerMutex_);

  int nSyncFailures = 0;
  while (true) {
    auto persistence = getPersistence();
    if (stopSyncer_) {
      if (!persistence ||
          !cache_.hasChangedSince(persistence->getLastPersistedVersion())) {
        break;
      }
    }

    if (persistence && !syncNow(*persistence)) {
      // track failures and give up if we tried too many times
      ++nSyncFailures;
      if (nSyncFailures == nSyncRetries_) {
        persistence->setPersistedVersion(cache_.getVersion());
        nSyncFailures = 0;
      }
    } else {
      nSyncFailures = 0;
    }

    if (!stopSyncer_) {
      stopSyncerCV_.wait_for(stopSyncerLock, syncInterval_);
    }
  }
}

template<typename K, typename V, typename MutexT>
bool LRUPersistentCache<K, V, MutexT>::syncNow(
    CachePersistence<K, V>& persistence ) {
  // check if we need to sync.  There is a chance that someone can
  // update cache_ between this check and the convert below, but that
  // is ok.  The persistence layer would have needed to update anyway
  // and will just get the latest version.
  if (!cache_.hasChangedSince(persistence.getLastPersistedVersion())) {
    // nothing to do
    return true;
  }

  // serialize the current contents of cache under lock
  auto serializedCacheAndVersion = cache_.convertToKeyValuePairs();
  if (!serializedCacheAndVersion) {
    LOG(ERROR) << "Failed to convert cache for serialization.";
    return false;
  }

  auto& kvPairs = std::get<0>(serializedCacheAndVersion.value());
  auto& version = std::get<1>(serializedCacheAndVersion.value());
  auto persisted =
    persistence.persistVersionedData(std::move(kvPairs), version);

  return persisted;
}

template<typename K, typename V, typename MutexT>
std::shared_ptr<CachePersistence<K, V>>
LRUPersistentCache<K, V, MutexT>::getPersistence() {
  typename wangle::CacheLockGuard<MutexT>::Read readLock(persistenceLock_);
  return persistence_;
}

template<typename K, typename V, typename MutexT>
void LRUPersistentCache<K, V, MutexT>::setPersistenceHelper(
    std::unique_ptr<CachePersistence<K, V>> persistence,
    bool syncVersion) noexcept {
  typename wangle::CacheLockGuard<MutexT>::Write writeLock(persistenceLock_);
  persistence_ = std::move(persistence);
  // load the persistence data into memory
  if (persistence_) {
    auto version = load(*persistence_);
    if (syncVersion) {
      persistence_->setPersistedVersion(version);
    }
  }
}

template<typename K ,typename V, typename MutexT>
void LRUPersistentCache<K, V, MutexT>::setPersistence(
    std::unique_ptr<CachePersistence<K, V>> persistence) {
  // note that we don't set the persisted version on the persistence like we
  // do in the constructor since we want any deltas that were in memory but
  // not in the persistence layer to sync back.
  setPersistenceHelper(std::move(persistence), false);
}

template<typename K, typename V, typename MutexT>
CacheDataVersion LRUPersistentCache<K, V, MutexT>::load(
    CachePersistence<K, V>& persistence) noexcept {
  auto kvPairs = persistence.load();
  if (!kvPairs) {
    return false;
  }
  return cache_.loadData(kvPairs.value());
}

}
