add_header_library(
  mutex
  HDRS
    mutex.h
  DEPENDS
    libc.src.__support.threads.mutex_common
)
