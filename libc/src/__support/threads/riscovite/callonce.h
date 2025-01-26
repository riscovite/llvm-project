//===-- RISCovite callonce fastpath ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_CALLONCE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_CALLONCE_H

#include "src/__support/macros/config.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/threads/callonce.h"

namespace LIBC_NAMESPACE_DECL {
using CallOnceFlag = unsigned long; // FIXME: Something else?

namespace callonce_impl {

LIBC_INLINE bool callonce_fastpath(CallOnceFlag *flag) {
  (void)flag;
  return false;
}
} // namespace callonce_impl

} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_RISCOVITE_CALLONCE_H
