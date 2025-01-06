add_header_library(
  riscovite_riscv_util
  HDRS
    syscall.h
  DEPENDS
    libc.src.__support.common
)
