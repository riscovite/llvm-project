if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_ARCHITECTURE})
  return()
endif()

add_subdirectory(${LIBC_TARGET_ARCHITECTURE})

add_object_library(
  riscovite_util
  SRCS
    exit.cpp
  HDRS
    syscall.h
    sysfuncs.h
    sysfunc_types.h
    io.h
  DEPENDS
    .${LIBC_TARGET_ARCHITECTURE}.riscovite_${LIBC_TARGET_ARCHITECTURE}_util
    libc.src.__support.common
    libc.src.__support.CPP.string_view
    libc.src.errno.errno
    libc.hdr.fcntl_macros
    libc.hdr.types.struct_flock
    libc.hdr.types.struct_flock64
    libc.hdr.types.struct_f_owner_ex
    libc.hdr.types.off_t
)
