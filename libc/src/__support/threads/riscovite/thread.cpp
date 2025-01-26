//===--- Implementation of Thread for RISCovite -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/thread.h"
#include "config/app.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/OSUtil/syscall.h" // For syscall functions.
#include "src/__support/common.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include "src/errno/libc_errno.h" // For error macros

#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {

static constexpr ErrorOr<size_t> add_no_overflow(size_t lhs, size_t rhs) {
  if (lhs > SIZE_MAX - rhs)
    return Error{EINVAL};
  if (rhs > SIZE_MAX - lhs)
    return Error{EINVAL};
  return lhs + rhs;
}

static constexpr ErrorOr<size_t> round_to_page(size_t v) {
  auto vp_or_err = add_no_overflow(v, 4096 - 1);
  if (!vp_or_err)
    return vp_or_err;

  return vp_or_err.value() & -4096;
}

int Thread::run(ThreadStyle style, ThreadRunner runner, void *arg, void *stack,
                size_t stacksize, size_t guardsize, bool detached) {
  (void)style;
  (void)runner;
  (void)arg;
  (void)stack;
  (void)stacksize;
  (void)guardsize;
  (void)detached;

  auto round_or_err = round_to_page(stacksize);
  if (!round_or_err)
    return round_or_err.error();
  stacksize = round_or_err.value();
  round_or_err = round_to_page(guardsize);
  if (!round_or_err)
    return round_or_err.error();
  guardsize = round_or_err.value();

  size_t guard_pages = guardsize >> 12;
  if (guard_pages > 65535) {
    return EINVAL; // create_memory_block requires this packed into 16 bytes
  }

  // We'll request a memory block that is readable and writable but not
  // executable, and that has the requested number of guard pages.
  uint64_t memblock_flags = 0b011 | ((uint64_t)((uint16_t)guard_pages) << 32);
  // TODO: Implement the rest of this.
  (void)memblock_flags;
  return ENOTSUP;
}

int Thread::join(ThreadReturnValue &retval) {
  // TODO: Implement
  (void)retval;
  return ENOENT;
}

int Thread::detach() {
  // TODO: Implement
  return int(DetachType::SIMPLE);
}

void Thread::wait() {
  // TODO: Implement
}

bool Thread::operator==(const Thread &thread) const {
  // TODO: Implement
  (void)thread;
  return false;
}

int Thread::set_name(const cpp::string_view &name) {
  // TODO: Implement
  (void)name;
  return ERANGE;
}

int Thread::get_name(cpp::StringStream &name) const {
  // TODO: Implement
  (void)name;
  return ERANGE;
}

} // namespace LIBC_NAMESPACE_DECL
