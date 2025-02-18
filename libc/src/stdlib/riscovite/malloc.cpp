//===-- Stub malloc for RISCovite -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/malloc.h"
#include "src/__support/freelist_heap.h"
#include "src/__support/macros/config.h"
#include "src/stdlib/riscovite/dlmalloc.h"

#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void *, malloc, (size_t size)) {
  return dlmalloc(size);
}

} // namespace LIBC_NAMESPACE_DECL
