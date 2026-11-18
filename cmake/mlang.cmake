# MLang is a host tool, built separately from the AArch64 CMake toolchain.
set(MLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/subprojects/mlang")
set(MLANG_COMPILER "${MLANG_ROOT}/build/mlang")
if(NOT EXISTS "${MLANG_COMPILER}")
    message(STATUS "Fetching and building MLang v0.4.4")
    execute_process(COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE MLANG_BOOTSTRAP_RESULT)
    if(NOT MLANG_BOOTSTRAP_RESULT EQUAL 0)
        message(FATAL_ERROR "MLang bootstrap failed; run ./bootstrap.sh for details")
    endif()
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MLANG_ROOT}" describe --tags --exact-match
    OUTPUT_VARIABLE MLANG_TAG OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE MLANG_TAG_RESULT)
if(NOT MLANG_TAG_RESULT EQUAL 0 OR NOT MLANG_TAG STREQUAL "v0.4.4")
    message(FATAL_ERROR "subprojects/mlang must remain pinned to the v0.4.4 release")
endif()
execute_process(COMMAND "${MLANG_COMPILER}" --version
    OUTPUT_VARIABLE MLANG_VERSION ERROR_VARIABLE MLANG_VERSION_ERROR
    RESULT_VARIABLE MLANG_VERSION_RESULT)
if(NOT MLANG_VERSION_RESULT EQUAL 0 OR NOT "${MLANG_VERSION}${MLANG_VERSION_ERROR}" MATCHES "0\\.4\\.4([^0-9]|$)")
    message(FATAL_ERROR "The firmware requires MLang v0.4.4; run ./bootstrap.sh")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(MLANG_CLANG clang HINTS ${LLVM_HINTS} REQUIRED)
file(GLOB MLANG_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/mlang/*.mla")
set(MLANG_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/weather_mlang.o")
add_custom_command(OUTPUT "${MLANG_OBJECT}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_mlang.py"
        --compiler "${MLANG_COMPILER}" --clang "${MLANG_CLANG}"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/src/mlang/application.mla"
        --output "${MLANG_OBJECT}"
    DEPENDS ${MLANG_SOURCES} "${MLANG_COMPILER}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_mlang.py"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/weather_mlang.ll"
    COMMENT "Compiling MLang application for bare-metal AArch64"
    VERBATIM)
set_source_files_properties("${MLANG_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
