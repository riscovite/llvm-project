//===-------------------- RISCovite syscalls --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_RISCOVITE_SYSCALL_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_RISCOVITE_SYSCALL_H

#include "src/__support/CPP/bit.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/macros/properties/architectures.h"

#ifdef LIBC_TARGET_ARCH_IS_ANY_RISCV
#include "riscv/syscall.h"
#else
#error "RISCovite OS target is only supported for RISC-V architecture"
#endif

namespace LIBC_NAMESPACE_DECL {

template <typename R, typename... Ts>

LIBC_INLINE SyscallResult<R> syscall_impl(long __number, Ts... ts) {
  static_assert(sizeof...(Ts) <= 6, "Too many arguments for syscall");
  return SyscallResult<R>(syscall_impl(__number, (long)ts...));
}

LIBC_INLINE constexpr uint64_t syscall_iface_func_num(uint8_t slot_num, uint32_t func_num) {
  return ~((uint64_t)(slot_num) | ((uint64_t)(func_num) << 4));
}

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_RISCOVITE_SYSCALL_H
