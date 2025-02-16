//===-- Implementation file of do_start -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "startup/riscovite/do_start.h"
#include "config/riscovite/app.h"
#include "src/__support/OSUtil/io.h"
#include "src/__support/OSUtil/riscovite/io.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/thread.h"
#include "src/stdlib/atexit.h"
#include "src/stdlib/exit.h"
#include "src/unistd/environ.h"
#include "startup/riscovite/auxvec.h"

#include <stdint.h>
#include <stdio.h>

extern "C" int main(int argc, char **argv, char **envp);

extern "C" {
// These arrays are present in the .init_array and .fini_array sections.
// The symbols are inserted by linker when it sees references to them.
extern uintptr_t __preinit_array_start[];
extern uintptr_t __preinit_array_end[];
extern uintptr_t __init_array_start[];
extern uintptr_t __init_array_end[];
extern uintptr_t __fini_array_start[];
extern uintptr_t __fini_array_end[];
} // extern "C"

namespace LIBC_NAMESPACE_DECL {
AppProperties app;

using InitCallback = void(int, char **, char **);
using FiniCallback = void(void);

typedef struct {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;
  uint64_t p_vaddr;
  uint64_t p_paddr;
  uint64_t p_filesz;
  uint64_t p_memsz;
  uint64_t p_align;
} Elf64_Phdr;
#define PT_PHDR 6
#define PT_TLS 7

static void call_init_array_callbacks(int argc, char **argv, char **env) {
  size_t preinit_array_size = __preinit_array_end - __preinit_array_start;
  for (size_t i = 0; i < preinit_array_size; ++i)
    reinterpret_cast<InitCallback *>(__preinit_array_start[i])(argc, argv, env);
  size_t init_array_size = __init_array_end - __init_array_start;
  for (size_t i = 0; i < init_array_size; ++i)
    reinterpret_cast<InitCallback *>(__init_array_start[i])(argc, argv, env);
}

static void call_fini_array_callbacks() {
  size_t fini_array_size = __fini_array_end - __fini_array_start;
  for (size_t i = fini_array_size; i > 0; --i)
    reinterpret_cast<FiniCallback *>(__fini_array_start[i - 1])();
}

static ThreadAttributes main_thread_attrib;
static TLSDescriptor tls;
// We separate teardown_main_tls from callbacks as callback function themselves
// may require TLS.
void teardown_main_tls() { cleanup_tls(tls.addr, tls.size); }

[[noreturn]] void do_start() {
  // Before _start calls this function it places a pointer to the incoming
  // arguments in app.args. The rest of "app" (an AppProperties object defined
  // above) is for us to populate here before we begin running the main program.

  // RISCovite just always uses 4k pages.
  app.page_size = 4096;

  // When the RISCovite supervisor transfers control to a new program, the
  // stack contains:
  // - argc
  // - argv pointers of length argc
  // - a null pointer to mark the end of argv
  // - environ pointers
  // - a null pointer to mark the end of environ
  // - auxillary vector entries
  // - a null auxillary vector entry to mark the end of the auxillary vector
  //
  // We're not provided with a direct pointer to the start of either environ
  // or auxval, so we need to calculate environ based on argc+argv, and then
  // find auxval by walking forward until we find the end of environ.
  uintptr_t *env_ptr = app.args->argv + app.args->argc + 1;
  uintptr_t *env_end_marker = env_ptr;
  app.env_ptr = env_ptr;
  while (*env_end_marker) {
    ++env_end_marker;
  }

  // We also provide the environment vector in the global variable "environ"
  // that POSIX requires in unistd.h. We don't aspire to be fully
  // POSIX-compliant, but we do make some effort on the basics to make it
  // easier to port software that targets the POSIX API without too many
  // modifications.
  environ = reinterpret_cast<char **>(env_ptr);

  // env_end_marker is now pointing at the NULL at the end of the environ
  // vector, and so the auxillary vector begins directly afterward.
  Elf64_Phdr *program_hdr_table = nullptr;
  uintptr_t program_hdr_count = 0;
  app.auxv_ptr = reinterpret_cast<AuxEntry *>(env_end_marker + 1);
  for (auto *aux_entry = app.auxv_ptr; aux_entry->id != AT_NULL; ++aux_entry) {
    switch (aux_entry->id) {
    case AT_PHDR:
      program_hdr_table = reinterpret_cast<Elf64_Phdr *>(aux_entry->value);
      break;
    case AT_PHNUM:
      program_hdr_count = aux_entry->value;
      break;
    default:
      break; // TODO: Read other useful entries from the aux vector.
    }
  }

  ptrdiff_t base = 0;
  app.tls.size = 0;
  Elf64_Phdr *tls_phdr = nullptr;

  for (uintptr_t i = 0; i < program_hdr_count; ++i) {
    Elf64_Phdr &phdr = program_hdr_table[i];
    if (phdr.p_type == PT_PHDR)
      base = reinterpret_cast<ptrdiff_t>(program_hdr_table) - phdr.p_vaddr;
    if (phdr.p_type == PT_TLS)
      tls_phdr = &phdr;
  }
  if (tls_phdr != nullptr) {
    app.tls.address = tls_phdr->p_vaddr + base;
    app.tls.size = tls_phdr->p_memsz;
    app.tls.init_size = tls_phdr->p_filesz;
    app.tls.align = tls_phdr->p_align;
  } else {
    app.tls.address = 0;
    app.tls.size = 0;
    app.tls.init_size = 0;
    app.tls.align = 1;
  }

  auto tls_alloc_size = get_tls_alloc_size(&app.tls);
  if (tls_alloc_size != 0) {
    // If the program eventually creates other threads then we'll use
    // dynamic memory allocation for their stacks and TLS areas, but
    // for the main thread we'll just place the TLS area on the main
    // stack since that avoids imposing a mandatory dynamic memory
    // allocation on a single-thread-only program.
    void *tls_area = __builtin_alloca(tls_alloc_size);
    init_tls(tls_area, tls);
  }
  if (tls.size != 0 && !set_thread_ptr(tls.tp)) {
    syscall_impl<long>(0x800 /* EXIT */, 1);
  }

  self.attrib = &main_thread_attrib;
  main_thread_attrib.atexit_callback_mgr =
      internal::get_thread_atexit_callback_mgr();

  atexit(call_fini_array_callbacks);

  call_init_array_callbacks(static_cast<int>(app.args->argc),
                            reinterpret_cast<char **>(app.args->argv),
                            reinterpret_cast<char **>(env_ptr));

  // We should now have the runtime environment configured enough to pass
  // control to the main program.
  int retval = main(static_cast<int>(app.args->argc),
                    reinterpret_cast<char **>(app.args->argv),
                    reinterpret_cast<char **>(env_ptr));

  exit(retval); // does not return
}

} // namespace LIBC_NAMESPACE_DECL
