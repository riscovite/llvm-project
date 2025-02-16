//===--- Implementation of a RISCovite mutex class --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_MUTEX_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_MUTEX_H

#include "config/app.h"
#include "hdr/types/pid_t.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/CPP/optional.h"
#include "src/__support/libc_assert.h"
#include "src/__support/macros/config.h"
#include "src/__support/macros/optimization.h"
#include "src/__support/threads/mutex_common.h"
#include "src/__support/threads/thread.h"

namespace LIBC_NAMESPACE_DECL {

class Mutex final {
private:
  cpp::Atomic<int> lock_count;
  ThreadWaitList waiters;
  uint8_t is_timed : 1;
  uint8_t is_recursive : 1;
  uint8_t is_robust : 1;
  uint8_t is_pshared : 1;

  MutexError lock_impl(cpp::optional<uint64_t> deadline);

public:
  LIBC_INLINE constexpr Mutex(bool is_timed, bool is_recursive, bool is_robust,
                              bool is_pshared)
      : lock_count(0), waiters(THREAD_LIST_DELAY_INIT), is_timed(is_timed),
        is_recursive(is_recursive), is_robust(is_robust),
        is_pshared(is_pshared) {}

  LIBC_INLINE static MutexError init(Mutex *mutex, bool is_timed, bool isrecur,
                                     bool isrobust, bool is_pshared) {
    // Delayed initialization of our thread list because otherwise we
    // need a global constructor to assign the list head to point to itself.
    mutex->waiters.init();
    mutex->lock_count = 0;
    mutex->is_timed = is_timed;
    mutex->is_recursive = isrecur;
    mutex->is_robust = isrobust;
    mutex->is_pshared = is_pshared;
    return MutexError::NONE;
  }

  LIBC_INLINE static MutexError destroy([[maybe_unused]] Mutex *lock) {
    // Nothing special to do on destroy.
    return MutexError::NONE;
  }

  LIBC_INLINE MutexError lock() {
    // We'll first try for an uncontended lock, in which case we can
    // keep this relatively cheap.
    if (LIBC_LIKELY(this->try_lock() == MutexError::NONE)) {
      return MutexError::NONE;
    }
    // If the lock is contended then we have more work to do.
    return this->lock_impl(cpp::nullopt);
  }

  LIBC_INLINE MutexError unlock() {
    int new_lock_count = this->lock_count.fetch_sub(-1);
    if (new_lock_count < 0) {
      // The state of this mutex is now broken, but unlocking
      // a thread that wasn't locked is undefined behavior so
      // we're not required to do anything reasonable here.
      return MutexError::UNLOCK_WITHOUT_LOCK;
    }
    if (new_lock_count == 0) {
      // If there are any waiters then we need to wake the
      // first one so that it can finish acquring the lock.
      auto mtstate = app.multithreading_state();
      if (mtstate != nullptr) {
        // If we're not in multithreaded mode at all then there can't
        // possibly be any other waiters, so we only do this work
        // if we have an mtstate.

        // The following wakes up the first waiting thread, if any.
        mtstate->end_thread_wait_success(&this->waiters);
      }
    }
    return MutexError::NONE;
  }

  LIBC_INLINE MutexError try_lock() {
    int lock_count = 0;
    if (this->lock_count.compare_exchange_strong(lock_count, 1)) {
      return MutexError::NONE;
    }
    return MutexError::BUSY;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_MUTEX_H
