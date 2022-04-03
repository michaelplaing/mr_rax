file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${mr_rax_SOURCE_DIR}/include/mr_rax/*.h")

add_executable(
    rax-test
    rax-test.c rc4rand.c crc16.c
    ${HEADER_LIST}
)

add_executable(
    topics
    topics.c
    ${HEADER_LIST}
)

target_link_libraries(rax-test PRIVATE mr_rax)
target_link_libraries(topics PRIVATE mr_rax)

add_test(NAME rax-test COMMAND rax-test)
add_test(NAME topics COMMAND topics)
