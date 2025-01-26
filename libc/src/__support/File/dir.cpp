//===--- Implementation of a platform independent Dir data structure ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dir.h"

#include "src/__support/CPP/mutex.h" // lock_guard
#include "src/__support/CPP/new.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include "src/errno/libc_errno.h" // For error macros

// FIXME: This Dir abstraction is currently very Linux-centric and not really
// portable to any OS whose platform layer doesn't directly produce a buffer
// of something that can be reinterpreted as struct dirent. The
// __RISCovite__-specific fragments are a temporary workaround for that;
// presumably at some point this abstraction will be improved upstream to
// support Windows (which has similar needs) and then we can hopefully benefit
// from that change too.
#ifdef __RISCovite__
#include "src/__support/OSUtil/riscovite/sysfunc_types.h"
#include <stdio.h>
#endif

namespace LIBC_NAMESPACE_DECL {

ErrorOr<Dir *> Dir::open(const char *path) {
  auto fd = platform_opendir(path);
  if (!fd)
    return LIBC_NAMESPACE::Error(fd.error());

  LIBC_NAMESPACE::AllocChecker ac;
  Dir *dir = new (ac) Dir(fd.value());
  if (!ac)
    return LIBC_NAMESPACE::Error(ENOMEM);
  return dir;
}

ErrorOr<struct ::dirent *> Dir::read() {
  cpp::lock_guard lock(mutex);

#ifdef __RISCovite__
  // Synthetic ".." and "." directory entries, since RISCovite doesn't
  // return those but we're doing our best to perform POSIXly here.
  if (fake_dotdot != 0) {
    LIBC_ASSERT(fake_dotdot <= 2);
    struct ::dirent *d = reinterpret_cast<struct ::dirent *>(&this->prev_entry);
    d->d_ino = 0;
    d->d_type = 2; // directory-like
    char *c = &d->d_name[0];
    for (int i = (fake_dotdot--); i > 0; i--) {
      *(c++) = '.';
    }
    *(c) = 0; // null terminator
    return d;
  }
#endif

  if (readptr >= fillsize) {
    auto readsize = platform_fetch_dirents(fd, buffer);
    if (!readsize)
      return LIBC_NAMESPACE::Error(readsize.error());
    fillsize = readsize.value();
    readptr = 0;
  }
  if (fillsize == 0)
    return nullptr;

#ifdef __RISCovite__
  // RISCovite's read_dir function returns entries that cannot be interpreted
  // directly as struct dirent, since there's no such thing as an "inode" on
  // RISCovite. Therefore we copy information from the current entry in the
  // buffer into a field of Dir and always return a pointer to that. Refer
  // to the comment on field prev_entry for more information.
  struct ::dirent *d = reinterpret_cast<struct ::dirent *>(&this->prev_entry);
  riscovite_dirent_t *src = reinterpret_cast<riscovite_dirent_t *>(buffer + readptr);
  d->d_ino = 0; // no inode concept on RISCovite
  d->d_type = src->resource_kind;
  __builtin_memcpy(&d->d_name[0], &src->name[0], src->name_len + 1 /* +null */);
  // NOTE: We're relying on the fact that struct_dirent.h declares only
  // exactly these three fields when targeting RISCovite. If that grows in
  // future then we'll need to at least write a placeholder value into each
  // new field to make sure they aren't garbage, but we're already producing
  // more than what POSIX strictly requires here.
  readptr += src->name_len + 3; // + type byte + length byte + null terminator byte.
#else
  struct ::dirent *d = reinterpret_cast<struct ::dirent *>(buffer + readptr);
#ifdef __linux__
  // The d_reclen field is available on Linux but not required by POSIX.
  readptr += d->d_reclen;
#else
  // Other platforms have to implement how the read pointer is to be updated.
#error "DIR read pointer update is missing."
#endif
#endif // __RISCovite__
  return d;
}

int Dir::close() {
  {
    cpp::lock_guard lock(mutex);
    int retval = platform_closedir(fd);
    if (retval != 0)
      return retval;
  }
  delete this;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
