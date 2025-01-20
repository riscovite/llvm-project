add_object_library(
  dlmalloc
  SRCS
    dlmalloc.c
  HDRS
    dlmalloc.h
)

add_entrypoint_object(
  abort
  SRCS
    abort.cpp
  HDRS
    ../abort.h
  DEPENDS
    libc.include.stdlib
    libc.src.stdlib._Exit
)

add_entrypoint_object(
  malloc
  SRCS
    malloc.cpp
  HDRS
    ../malloc.h
  DEPENDS
    .dlmalloc
)

add_entrypoint_object(
  free
  SRCS
    free.cpp
  HDRS
    ../free.h
  DEPENDS
    .dlmalloc
)

add_entrypoint_object(
  calloc
  SRCS
    calloc.cpp
  HDRS
    ../calloc.h
  DEPENDS
    .dlmalloc
)

add_entrypoint_object(
  realloc
  SRCS
    realloc.cpp
  HDRS
    ../realloc.h
  DEPENDS
    .dlmalloc
)

add_entrypoint_object(
  aligned_alloc
  SRCS
    aligned_alloc.cpp
  HDRS
    ../aligned_alloc.h
  DEPENDS
    .dlmalloc
)
