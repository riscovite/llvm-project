//===-- Implementation file of do_start -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// FIXME: Everything in here is currently just a hacky copy of the Linux startup
// code, and needs to be rewritten to make sense for RISCovite.

#include "startup/riscovite/do_start.h"
#include "config/riscovite/app.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/macros/config.h"
#include "src/stdlib/atexit.h"
#include "src/stdlib/exit.h"
#include "src/unistd/environ.h"

#include <stdint.h>

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

//static ThreadAttributes main_thread_attrib;
static TLSDescriptor tls;
// We separate teardown_main_tls from callbacks as callback function themselves
// may require TLS.
void teardown_main_tls() { cleanup_tls(tls.addr, tls.size); }

[[noreturn]] void do_start() {
  // Before _start calls this function it places a pointer to the incoming
  // arguments in app.args. The rest of "app" (an AppProperties object defined
  // above) is for us to populate here before we begin running the main program.

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
  app.auxv_ptr = reinterpret_cast<AuxEntry *>(env_end_marker + 1);

  // TODO: Actually walk over the aux vector so we can find the program
  // headers, which we'll need to find the  PT_TLS segment that acts as the
  // thread local storage initialization image. For now we just zero out all
  // of those fields because we don't have threading support implemented yet
  // anyway.
  app.tls.address = 0;
  app.tls.size = 0;
  app.tls.init_size = 0;
  app.tls.align = 0;

  // TODO: Once we actually have thread handling implemented, prepare the
  // main thread by pushing its TLS area onto the stack.

  // TODO: Set up the atexit table for the main thread.

  atexit(call_fini_array_callbacks);

  call_init_array_callbacks(static_cast<int>(app.args->argc),
                            reinterpret_cast<char **>(app.args->argv),
                            reinterpret_cast<char **>(env_ptr));

  // We should now have the runtime environment configured enough to pass
  // control to the main program.
  int retval = main(static_cast<int>(app.args->argc),
                    reinterpret_cast<char **>(app.args->argv),
                    //reinterpret_cast<char **>(env_ptr));
                    NULL);

  exit(retval); // does not return
}

} // namespace LIBC_NAMESPACE_DECL
