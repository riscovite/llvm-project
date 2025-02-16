//===-- Definition of a common mutex type ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES___MUTEX_TYPE_H
#define LLVM_LIBC_TYPES___MUTEX_TYPE_H

#include "__futex_word.h"

typedef struct {
  unsigned char __timed;
  unsigned char __recursive;
  unsigned char __robust;

  void *__owner;
  unsigned long long __lock_count;

#ifdef __linux__
  __futex_word __ftxw;
#elif defined(__RISCovite__)
  // This is really just to reserve some extra space for the internal
  // Mutex on this platform.
  // apparentlymart NOTE: the unconditionally-declared fields above seem
  // like they were originally intended to match the Linux implementation
  // of Mutex, but there's now an additional unsigned char is_pshared
  // there so it doesn't actually match anymore. I'm not sure exactly what
  // the goal was here but the following is assuming that the exact
  // layout of __mutex_type does not matter as long as it's at least
  // as big as the platform's Mutex type.
  char __padding[8];
#else
#error "Mutex type not defined for the target platform."
#endif
} __mutex_type;

#endif // LLVM_LIBC_TYPES___MUTEX_TYPE_H
