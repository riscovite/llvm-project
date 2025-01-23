//===--- RISCovite specialization of the File data structure --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/off_t.h"
#include "src/__support/File/file.h"
#include "src/__support/macros/config.h"

#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {

FileIOResult riscovite_file_write(File *, const void *, size_t);
FileIOResult riscovite_file_read(File *, void *, size_t);
ErrorOr<off_t> riscovite_file_seek(File *, off_t, int);
int riscovite_file_close(File *);

class RiscoviteFile : public File {
  uint64_t hnd_num;

public:
  constexpr RiscoviteFile(uint64_t hnd_num, uint8_t *buffer, size_t buffer_size,
                      int buffer_mode, bool owned, File::ModeFlags modeflags)
      : File(&riscovite_file_write, &riscovite_file_read, &riscovite_file_seek,
             &riscovite_file_close, buffer, buffer_size, buffer_mode, owned,
             modeflags),
        hnd_num(hnd_num) {}

  uint64_t get_hnd_num() const { return hnd_num; }
};

// Create a File object and associate it with an existing handle number.
ErrorOr<RiscoviteFile *> create_file_from_hnd_num(uint64_t hnd_num, const char *mode);

} // namespace LIBC_NAMESPACE_DECL
