//===----- RISCovite system function types ------------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_RISCOVITE_SYSFUNC_TYPES_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_RISCOVITE_SYSFUNC_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// riscovite_dirent_t represents the layout of a directory entry in a buffer
// that has been populated by the readdir system function.
//
// The layout allows entry names of up to 253 bytes, but the supervisor will
// actually pack the entries tightly so that the name field of one entry
// overlaps with the beginning of the next entry unless the entry name is
// exactly 253 bytes. Therefore consumers of such a buffer must advance by
// name_len+3 to find the next entry, instead of by sizeof(riscovite_dirent_t).
typedef struct {
  uint8_t resource_kind;
  uint8_t name_len; // in practice, always <=253

  // RISCovite supervisor guarantees that it will always leave enough trailing
  // room in the buffer given to readdir for this 254-element array to always
  // fit inside the given buffer, but elements >=name_len have unspecified
  // content. (For entries other than the last in the buffer, those elements
  // actually belong to the next entry in the buffer.)
  uint8_t name[254]; // null-terminated; offset name_len is always populated as zero
} riscovite_dirent_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
