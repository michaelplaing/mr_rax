find_library(JEMALLOC jemalloc REQUIRED)
# find_library(ZLOG zlog REQUIRED)

file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${mr_rax_SOURCE_DIR}/include/mr_rax/*.h")

add_library(
    mr_rax SHARED
    mr_rax.c rax.c
    mr_rax_internal.h ${HEADER_LIST}
)

target_include_directories(mr_rax PUBLIC ../include)
target_link_libraries(mr_rax PUBLIC jemalloc)
