//===-- Stub malloc for RISCovite -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/calloc.h"
#include "src/__support/freelist_heap.h"
#include "src/__support/macros/config.h"

#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void *, calloc, (size_t num, size_t size)) {
  // TODO: Either select an existing allocator to use, or write one here.
  auto _ = num; // unused for now
  _ = size; // unused for now
  return NULL;
}

} // namespace LIBC_NAMESPACE_DECL
