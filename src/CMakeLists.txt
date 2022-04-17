find_library(JEMALLOC jemalloc REQUIRED)
# find_library(ZLOG zlog REQUIRED)

file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${mr_rax_SOURCE_DIR}/include/mr_rax/*.h")

add_library(
    mr_rax SHARED
    mr_rax.c rax.c
    rax_internal.h mr_rax_internal.h ${HEADER_LIST}
)

target_include_directories(mr_rax PUBLIC ../include)
target_link_libraries(mr_rax PUBLIC jemalloc)

if(RAX_DEBUG_MSG)
    set_target_properties(mr_rax PROPERTIES COMPILE_DEFINITIONS "RAX_DEBUG_MSG=1")
    message(STATUS "Compiling with debug messages turned on")
endif()
