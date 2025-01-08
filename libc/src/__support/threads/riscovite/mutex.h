//===--- Implementation of a RISCovite mutex class --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_MUTEX_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_MUTEX_H

#include "hdr/types/pid_t.h"
#include "src/__support/CPP/optional.h"
#include "src/__support/libc_assert.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/mutex_common.h"

#pragma clang diagnostic ignored "-Wunused-parameter"

namespace LIBC_NAMESPACE_DECL {

// FIXME: This is currently just a stub which needs implementing.
class Mutex final {

public:
  LIBC_INLINE constexpr Mutex(bool is_timed, bool is_recursive, bool is_robust,
                              bool is_pshared) {}

  LIBC_INLINE static MutexError init(Mutex *mutex, bool is_timed, bool isrecur,
                                     bool isrobust, bool is_pshared) {
    return MutexError::NONE;
  }

  LIBC_INLINE static MutexError destroy(Mutex *lock) {
    LIBC_ASSERT(false && "Mutex not yet implemented.");
    return MutexError::NONE;
  }

  LIBC_INLINE MutexError lock() {
    LIBC_ASSERT(false && "Mutex not yet implemented.");
    return MutexError::NONE;
  }

  LIBC_INLINE MutexError unlock() {
    LIBC_ASSERT(false && "Mutex not yet implemented.");
    return MutexError::UNLOCK_WITHOUT_LOCK;
  }

  LIBC_INLINE MutexError try_lock() {
    LIBC_ASSERT(false && "Mutex not yet implemented.");
    return MutexError::BUSY;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_MUTEX_H
