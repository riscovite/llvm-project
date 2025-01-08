//===-- Implementation of tls for riscv -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/thread.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "startup/riscovite/do_start.h"

#pragma clang diagnostic ignored "-Wunused-parameter"

namespace LIBC_NAMESPACE_DECL {

// TEMP: Stubbed until we have a real implementation
struct TLSDescriptor;

void init_tls(TLSDescriptor &tls_descriptor) {
  // TODO: Implement
}

void cleanup_tls(uintptr_t addr, uintptr_t size) {
  // TODO: Implement
}

bool set_thread_ptr(uintptr_t val) {
  LIBC_INLINE_ASM("mv tp, %0\n\t" : : "r"(val));
  return true;
}
} // namespace LIBC_NAMESPACE_DECL
