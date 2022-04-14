file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${mr_rax_SOURCE_DIR}/include/mr_rax/*.h")
#file(GLOB HEADER_LIST_INTERNAL CONFIGURE_DEPENDS "${mr_rax_SOURCE_DIR}/src/*.h")

add_executable(
    rax-test
    rax-test.c rc4rand.c crc16.c
    ${HEADER_LIST}
)

add_executable(
    topics
    topics.c
    ../src/mr_rax_internal.h
    ${HEADER_LIST}
)

add_executable(
    nextchild
    nextchild.c
    ../src/mr_rax_internal.h
    ${HEADER_LIST}
)

target_link_libraries(rax-test PRIVATE mr_rax)

target_link_libraries(topics PRIVATE mr_rax)
target_include_directories(topics PRIVATE ../src)

target_link_libraries(nextchild PRIVATE mr_rax)
target_include_directories(nextchild PRIVATE ../src)

add_test(NAME rax-test COMMAND rax-test)
add_test(NAME topics COMMAND topics)
add_test(NAME nextchild COMMAND nextchild)
