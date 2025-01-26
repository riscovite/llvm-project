add_object_library(
  thread
  SRCS
    thread.cpp
  DEPENDS
    libc.config.app_h
    libc.include.sys_syscall
    libc.hdr.fcntl_macros
    libc.src.errno.errno
    libc.src.__support.CPP.atomic
    libc.src.__support.CPP.stringstream
    libc.src.__support.CPP.string_view
    libc.src.__support.common
    libc.src.__support.error_or
    libc.src.__support.threads.thread_common
  COMPILE_OPTIONS
    ${libc_opt_high_flag}
    -fno-omit-frame-pointer # This allows us to sniff out the thread args from
                            # the new thread's stack reliably.
    -Wno-frame-address      # Yes, calling __builtin_return_address with a
                            # value other than 0 is dangerous. We know.
)

add_header_library(
  mutex
  HDRS
    mutex.h
  DEPENDS
    libc.src.__support.threads.mutex_common
)

add_object_library(
  callonce
  SRCS
    callonce.cpp
  HDRS
    ../callonce.h
    callonce.h
  DEPENDS
    libc.src.__support.macros.optimization
)
