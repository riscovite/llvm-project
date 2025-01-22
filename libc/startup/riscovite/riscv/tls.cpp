//===-- Implementation of tls for riscv -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/riscovite/app.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/thread.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "startup/riscovite/do_start.h"

#pragma clang diagnostic ignored "-Wunused-parameter"

namespace LIBC_NAMESPACE_DECL {

struct TLSDescriptor;

constexpr uintptr_t get_tls_tp_offset(uintptr_t alignment) {
  // riscv64 follows the variant 1 TLS layout:
  constexpr uintptr_t size_of_pointers = 2 * sizeof(uintptr_t);
  uintptr_t padding = 0;
  const uintptr_t ALIGNMENT_MASK = alignment - 1;
  uintptr_t diff = size_of_pointers & ALIGNMENT_MASK;
  if (diff != 0)
    padding += (ALIGNMENT_MASK - diff) + 1;

  return size_of_pointers + padding;
}

uintptr_t get_tls_alloc_size(const TLSImage *tls) {
  if (tls->size == 0) {
    // No TLS at all, then.
    return 0;
  }

  const uintptr_t offset = get_tls_tp_offset(tls->align);
  return offset + app.tls.size;
}

void init_tls(void *alloc, TLSDescriptor &tls_descriptor) {
  const uintptr_t tp_offset = get_tls_tp_offset(app.tls.align);
  uintptr_t *ptrs = (uintptr_t *)alloc;
  ptrs[0] = 0;
  ptrs[1] = 0;

  void *tls = (char *)alloc + tp_offset;
  inline_memcpy(tls, (void *)app.tls.address, app.tls.init_size);
  __builtin_memset((char *)tls + app.tls.init_size, 0, app.tls.size - app.tls.init_size);
  tls_descriptor.addr = (uintptr_t)alloc;
  tls_descriptor.size = tp_offset + app.tls.size;
  tls_descriptor.tp = (uintptr_t)tls;
}

void cleanup_tls(uintptr_t addr, uintptr_t size) {
  // TODO: Implement
}

bool set_thread_ptr(uintptr_t val) {
  LIBC_INLINE_ASM("mv tp, %0\n\t" : : "r"(val));
  return true;
}
} // namespace LIBC_NAMESPACE_DECL
