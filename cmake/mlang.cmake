# MLang is a host tool, built separately from the AArch64 CMake toolchain.
if(DEFINED ENV{MLANG_ROOT})
    set(MLANG_DEFAULT_ROOT "$ENV{MLANG_ROOT}")
else()
    set(MLANG_DEFAULT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/subprojects/mlang")
endif()
set(MLANG_ROOT "${MLANG_DEFAULT_ROOT}" CACHE PATH "MLang host compiler checkout")
get_filename_component(MLANG_ROOT "${MLANG_ROOT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(MLANG_COMPILER "${MLANG_ROOT}/build/mlang")
set(MLANG_NEEDS_BOOTSTRAP OFF)
if(MLANG_ROOT STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/subprojects/mlang")
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MLANG_ROOT}" branch --show-current
        OUTPUT_VARIABLE MLANG_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT MLANG_BRANCH STREQUAL "main")
        set(MLANG_NEEDS_BOOTSTRAP ON)
    endif()
endif()
if(NOT EXISTS "${MLANG_COMPILER}" OR MLANG_NEEDS_BOOTSTRAP)
    message(STATUS "Fetching and building MLang main")
    if(MLANG_ROOT STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/subprojects/mlang")
        set(MLANG_BOOTSTRAP_COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap.sh")
    else()
        set(MLANG_BOOTSTRAP_COMMAND "${CMAKE_COMMAND}" -E env "MLANG_ROOT=${MLANG_ROOT}"
            sh "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap.sh")
    endif()
    execute_process(COMMAND ${MLANG_BOOTSTRAP_COMMAND}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE MLANG_BOOTSTRAP_RESULT)
    if(NOT MLANG_BOOTSTRAP_RESULT EQUAL 0)
        message(FATAL_ERROR "MLang bootstrap failed; run ./bootstrap.sh for details")
    endif()
endif()
execute_process(COMMAND "${MLANG_COMPILER}" --version
    OUTPUT_VARIABLE MLANG_VERSION ERROR_VARIABLE MLANG_VERSION_ERROR
    RESULT_VARIABLE MLANG_VERSION_RESULT)
if(NOT MLANG_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR "Cannot run the MLang host compiler; run ./bootstrap.sh")
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
