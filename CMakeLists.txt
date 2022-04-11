cmake_minimum_required(VERSION 3.22)

project(
    mr_rax VERSION 0.1.0
    DESCRIPTION "MQTT5 data requirements functions"
    LANGUAGES C # possibly add CXX for Catch2
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(CMAKE_C_STANDARD 23)
    set(CMAKE_C_EXTENSIONS OFF)

    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    include(CTest)

    find_package(
        Doxygen
        REQUIRED dot
        OPTIONAL_COMPONENTS mscgen dia
    )

    if(Doxygen_FOUND)
        # add_subdirectory(docs) # TODO
    else()
        message(STATUS "Doxygen not found, not building docs")
    endif()
endif()

file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)

if(EXISTS "${LOC_PATH}")
    message(
        FATAL_ERROR "You cannot build in a source directory "
        "(or any directory with a CMakeLists.txt file). "
        "Please make a build subdirectory and use that. "
        "Remove the CMakeCache.txt and CMakeFiles/ just created."
    )
endif()

option(UNIT_TESTING "Build with unit tests" ON)

if (UNIT_TESTING)
    set(BUILD_STATIC_LIB ON)
    add_subdirectory(tests)
endif()

add_subdirectory(src)
