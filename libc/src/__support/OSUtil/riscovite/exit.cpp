//===-------- RISCovite implementation of an exit function ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/riscovite/syscall.h" // syscall_impl
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

__attribute__((noreturn)) void
exit(int status) {
  for (;;) {
    LIBC_NAMESPACE::syscall_impl<long>(0x800, status);
  }
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
