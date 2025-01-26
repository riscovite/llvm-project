//===-- RISCovite implementation of the callonce function slow path  ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/callonce.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/riscovite/callonce.h"

namespace LIBC_NAMESPACE_DECL {
namespace callonce_impl {
int callonce_slowpath(CallOnceFlag *flag, CallOnceCallback *func) {
  (void)flag;
  (void)func;
  return 0;
}
} // namespace callonce_impl
} // namespace LIBC_NAMESPACE_DECL
