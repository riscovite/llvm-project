//===--- RISCovite implementation of the Dir helpers ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/File/dir.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/OSUtil/riscovite/sysfuncs.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"

#include "hdr/fcntl_macros.h" // For open flags

#include <errno.h>

namespace LIBC_NAMESPACE_DECL {

ErrorOr<int> platform_opendir(const char *path) {
  uint64_t open_flags = RISCOVITE_OPEN_DIR;
  SyscallResult<uint64_t> result = LIBC_NAMESPACE::syscall_impl<uint64_t>(RISCOVITE_SYS_OPEN, RISCOVITE_HND_CWD, path, open_flags);
  if (result.error != 0) {
    return Error(static_cast<int>(result.error));
  }
  return static_cast<int>(result.value);
}

ErrorOr<size_t> platform_fetch_dirents(int fd, cpp::span<uint8_t> buffer) {
  auto result = LIBC_NAMESPACE::syscall_impl<size_t>(RISCOVITE_SYS_READ_DIR, (uint64_t)fd, buffer.data(), buffer.size(), ~(uint64_t)0);
  if (result.error != 0) {
    return LIBC_NAMESPACE::Error(static_cast<int>(result.error));
  }
  return result.value;
}

int platform_closedir(int hnd_num) {
  auto result = LIBC_NAMESPACE::syscall_impl<int>(RISCOVITE_SYS_CLOSE, hnd_num);
  if (result.error != 0) {
    return static_cast<int>(result.error);
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
