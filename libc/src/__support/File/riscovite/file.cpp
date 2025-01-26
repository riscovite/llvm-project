//===--- Implementation of the RISCovite specialization of File -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "file.h"

#include "hdr/stdio_macros.h"
#include "hdr/types/off_t.h"
#include "src/__support/CPP/new.h"
#include "src/__support/File/file.h"
#include "src/__support/OSUtil/fcntl.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/OSUtil/riscovite/sysfuncs.h"
#include "src/__support/macros/config.h"
#include "src/errno/libc_errno.h" // For error macros

#include "hdr/fcntl_macros.h" // For mode_t and other flags to the open syscall
#include <sys/stat.h>    // For S_IS*, S_IF*, and S_IR* flags.
#include <stdint.h>
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

FileIOResult riscovite_file_write(File *f, const void *data, size_t size) {
  auto *lf = reinterpret_cast<RiscoviteFile *>(f);
  SyscallResult<uint64_t> result =
      LIBC_NAMESPACE::syscall_impl<uint64_t>(RISCOVITE_SYS_WRITE, lf->get_hnd_num(), data, size);
  return {(size_t)result.value, (int)result.error};
}

FileIOResult riscovite_file_read(File *f, void *buf, size_t size) {
  auto *lf = reinterpret_cast<RiscoviteFile *>(f);
  SyscallResult<uint64_t> result =
      LIBC_NAMESPACE::syscall_impl<int>(RISCOVITE_SYS_READ, lf->get_hnd_num(), buf, size);
  return {(size_t)result.value, (int)result.error};
}

ErrorOr<off_t> riscovite_file_seek(File *f, off_t offset, int whence) {
  // TODO: Implement
  return ErrorOr<off_t>(cpp::unexpected(EINVAL));
}

int riscovite_file_close(File *f) {
  auto *lf = reinterpret_cast<RiscoviteFile *>(f);
  SyscallResult<uint64_t> result = LIBC_NAMESPACE::syscall_impl<int>(RISCOVITE_SYS_CLOSE, lf->get_hnd_num());
  if (result.error != 0) {
    return (int)result.error;
  }
  delete lf;
  return 0;
}

ErrorOr<File *> openfile(const char *path, const char *mode) {
  using ModeFlags = File::ModeFlags;
  auto modeflags = File::mode_flags(mode);
  if (modeflags == 0) {
    // return {nullptr, EINVAL};
    return Error(EINVAL);
  }
  uint64_t open_flags = RISCOVITE_OPEN_FILE;
  if (modeflags & ModeFlags(File::OpenMode::APPEND)) {
    open_flags = RISCOVITE_OPEN_CREATE | RISCOVITE_OPEN_APPEND;
    if (modeflags & ModeFlags(File::OpenMode::PLUS))
      open_flags |= RISCOVITE_OPEN_TO_READWRITE;
    else
      open_flags |= RISCOVITE_OPEN_TO_WRITE;
  } else if (modeflags & ModeFlags(File::OpenMode::WRITE)) {
    //open_flags = O_CREAT | O_TRUNC;
    open_flags = RISCOVITE_OPEN_CREATE | RISCOVITE_OPEN_TRUNCATE;
    if (modeflags & ModeFlags(File::OpenMode::PLUS))
      open_flags |= RISCOVITE_OPEN_TO_READWRITE;
    else
      open_flags |= RISCOVITE_OPEN_TO_WRITE;
  } else {
    if (modeflags & ModeFlags(File::OpenMode::PLUS))
      open_flags |= RISCOVITE_OPEN_TO_READWRITE;
    else
      open_flags |= RISCOVITE_OPEN_TO_READ;
  }

  SyscallResult<uint64_t> result = LIBC_NAMESPACE::syscall_impl<uint64_t>(RISCOVITE_SYS_OPEN, RISCOVITE_HND_CWD, path, open_flags);
  if (result.error != 0) {
    return Error((int)result.error);
  }
  uint64_t hnd_num = result.value;

  uint8_t *buffer;
  {
    AllocChecker ac;
    buffer = new (ac) uint8_t[File::DEFAULT_BUFFER_SIZE];
    if (!ac)
      return Error(ENOMEM);
  }
  AllocChecker ac;
  auto *file = new (ac)
      RiscoviteFile(hnd_num, buffer, File::DEFAULT_BUFFER_SIZE, _IOFBF, true, modeflags);
  if (!ac) {
    if (buffer != nullptr) {
      // If we previously allocated a buffer then we don't want to leak it.
      delete[](buffer);
    }
    return Error(ENOMEM);
  }
  return file;
}

ErrorOr<RiscoviteFile *> create_file_from_hnd_num(uint64_t hnd_num, const char *mode) {
  using ModeFlags = File::ModeFlags;
  ModeFlags modeflags = File::mode_flags(mode);
  if (modeflags == 0) {
    return Error(EINVAL);
  }

  using OpenMode = File::OpenMode;
  bool do_seek = false;
  if ((modeflags & static_cast<ModeFlags>(OpenMode::APPEND))) {
    do_seek = true;
  }

  uint8_t *buffer;
  {
    AllocChecker ac;
    buffer = new (ac) uint8_t[File::DEFAULT_BUFFER_SIZE];
    if (!ac) {
      return Error(ENOMEM);
    }
  }
  AllocChecker ac;
  auto *file = new (ac)
      RiscoviteFile(hnd_num, buffer, File::DEFAULT_BUFFER_SIZE, _IOFBF, true, modeflags);
  if (!ac) {
    return Error(ENOMEM);
  }
  if (do_seek) {
    auto result = file->seek(0, SEEK_END);
    if (!result.has_value()) {
      free(file);
      return Error(result.error());
    }
  }
  return file;
}

int get_fileno(File *f) {
  auto *lf = reinterpret_cast<RiscoviteFile *>(f);
  return (int)lf->get_hnd_num();
}

} // namespace LIBC_NAMESPACE_DECL
