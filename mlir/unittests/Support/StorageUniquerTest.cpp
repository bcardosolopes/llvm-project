//===- StorageUniquerTest.cpp - StorageUniquer Tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Support/StorageUniquer.h"
#include "mlir/Support/ThreadLocalCache.h"
#include "gmock/gmock.h"

#include <future>
#include <thread>

using namespace mlir;

namespace {
/// Simple storage class used for testing.
template <typename ConcreteT, typename... Args>
struct SimpleStorage : public StorageUniquer::BaseStorage {
  using Base = SimpleStorage<ConcreteT, Args...>;
  using KeyTy = std::tuple<Args...>;

  SimpleStorage(KeyTy key) : key(key) {}

  /// Get an instance of this storage instance.
  template <typename... ParamsT>
  static ConcreteT *get(StorageUniquer &uniquer, ParamsT &&...params) {
    return uniquer.get<ConcreteT>(
        /*initFn=*/{}, std::make_tuple(std::forward<ParamsT>(params)...));
  }

  /// Construct an instance with the given storage allocator.
  static ConcreteT *construct(StorageUniquer::StorageAllocator &alloc,
                              KeyTy key) {
    return new (alloc.allocate<ConcreteT>())
        ConcreteT(std::forward<KeyTy>(key));
  }
  bool operator==(const KeyTy &key) const { return this->key == key; }

  KeyTy key;
};
} // namespace

TEST(StorageUniquerTest, NonTrivialDestructor) {
  struct NonTrivialStorage : public SimpleStorage<NonTrivialStorage, bool *> {
    using Base::Base;
    ~NonTrivialStorage() {
      bool *wasDestructed = std::get<0>(key);
      *wasDestructed = true;
    }
  };

  // Verify that the storage instance destructor was properly called.
  bool wasDestructed = false;
  {
    StorageUniquer uniquer;
    uniquer.registerParametricStorageType<NonTrivialStorage>();
    NonTrivialStorage::get(uniquer, &wasDestructed);
  }

  EXPECT_TRUE(wasDestructed);
}

TEST(StorageUniquerTest, ClearThreadLocalCaches) {
  struct IntStorage : public SimpleStorage<IntStorage, int> {
    using Base::Base;
  };
  StorageUniquer uniquer;
  uniquer.registerParametricStorageType<IntStorage>();
  IntStorage *one = IntStorage::get(uniquer, 1);
  std::thread([&] { EXPECT_EQ(IntStorage::get(uniquer, 1), one); }).join();

  // Instances outlive the caches that found them.
  uniquer.clearThreadLocalCaches();
  EXPECT_EQ(IntStorage::get(uniquer, 1), one);
  EXPECT_NE(IntStorage::get(uniquer, 2), one);
}

TEST(ThreadLocalCacheTest, ClearResetsLiveThreadValue) {
  ThreadLocalCache<int> cache;
  std::promise<void> written;
  std::future<void> writtenFuture = written.get_future();
  std::promise<void> cleared;
  std::future<void> clearedFuture = cleared.get_future();
  std::thread worker([&] {
    int &value = cache.get();
    value = 42;
    written.set_value();
    clearedFuture.wait();
    // The thread keeps its instance; only the value is reset.
    EXPECT_EQ(&cache.get(), &value);
    EXPECT_EQ(cache.get(), 0);
  });
  writtenFuture.wait();
  cache.clear();
  cleared.set_value();
  worker.join();
}
