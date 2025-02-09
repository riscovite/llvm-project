//===-- Implementation of _start for riscv --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/__support/macros/attributes.h"
#include "startup/riscovite/do_start.h"

extern "C" [[noreturn]] void _start() {
  // Before we get too far we need to set up the global pointer and
  // try to grow the stack to 128kiB to give us a little more headroom.
  asm volatile(".option push\n\t"
               ".option norelax\n\t"
               "lla gp, __global_pointer$\n\t"
               "li a1, 0x20000\n\t" // 128k
               "sub a0, sp, a1\n\t" // a0 = sp - 128k
               "li a7, 0x860\n\t" // SYS_SET_STACK_BOUND
               "ecall\n\t" // call the system function to grow the stack
               ".option pop\n\t");
  // Fetch the args using the frame pointer.
  LIBC_NAMESPACE::app.args = reinterpret_cast<LIBC_NAMESPACE::Args *>(
      reinterpret_cast<uintptr_t *>(__builtin_frame_address(0)));
  LIBC_NAMESPACE::do_start();
}
