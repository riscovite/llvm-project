//===--- Definition of RISCovite stdin ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "file.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/FILE.h"
#include "src/__support/macros/config.h"
#include "src/__support/OSUtil/riscovite/sysfuncs.h"

namespace LIBC_NAMESPACE_DECL {

constexpr size_t STDIN_BUFFER_SIZE = 512;
uint8_t stdin_buffer[STDIN_BUFFER_SIZE];
static RiscoviteFile StdIn(RISCOVITE_HND_STDIN, stdin_buffer, STDIN_BUFFER_SIZE,
                           _IOFBF, false,
                           File::ModeFlags(File::OpenMode::READ));
File *stdin = &StdIn;

} // namespace LIBC_NAMESPACE_DECL

extern "C" {
FILE *stdin = reinterpret_cast<FILE *>(&LIBC_NAMESPACE::StdIn);
} // extern "C"
