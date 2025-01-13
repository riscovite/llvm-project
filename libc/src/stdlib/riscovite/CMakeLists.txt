add_entrypoint_object(
  malloc
  SRCS
    malloc.cpp
  HDRS
    ../malloc.h
)

add_entrypoint_object(
  free
  SRCS
    free.cpp
  HDRS
    ../free.h
)

add_entrypoint_object(
  calloc
  SRCS
    calloc.cpp
  HDRS
    ../calloc.h
)

add_entrypoint_object(
  realloc
  SRCS
    realloc.cpp
  HDRS
    ../realloc.h
)

add_entrypoint_object(
  aligned_alloc
  SRCS
    aligned_alloc.cpp
  HDRS
    ../aligned_alloc.h
)
