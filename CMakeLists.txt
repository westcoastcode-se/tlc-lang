cmake_minimum_required(VERSION 3.20)
project(tlc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -fno-exceptions)

set(LLVM_DIR "/usr/lib/cmake/llvm" CACHE PATH "Path to LLVMConfig.cmake" FORCE)
find_package(LLVM 23.1 REQUIRED CONFIG)
message(STATUS "tlc using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

separate_arguments(TLC_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")

add_library(tlc_lib
    src/driver.cpp
    src/source.cpp
    src/diag.cpp
    src/lexer.cpp
    src/ast.cpp
    src/parser.cpp
    src/parser_type.cpp
    src/parser_decl.cpp
    src/parser_stmt.cpp
    src/parser_expr.cpp
    src/sema.cpp
    src/sema_lookup.cpp
    src/sema_expr.cpp
    src/sema_unit.cpp
    src/sema_generic.cpp
    src/codegen.cpp
    src/codegen_abi.cpp
    src/codegen_lower.cpp
    src/codegen_emit.cpp
    src/codegen_debug.cpp
    src/pkg.cpp
    plugins/lsp/lsp.cpp
)
target_include_directories(tlc_lib PUBLIC "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/plugins/lsp" ${LLVM_INCLUDE_DIRS})
target_compile_options(tlc_lib PUBLIC ${TLC_LLVM_DEFINITIONS})
target_link_libraries(tlc_lib PUBLIC LLVM)

add_executable(tlc src/main.cpp)
target_link_libraries(tlc PRIVATE tlc_lib)

add_executable(tlc_tests
    tests/main.cpp
    tests/spawn.cpp
    tests/cli_test.cpp
    tests/diag_test.cpp
    tests/lexer_test.cpp
    tests/parser_test.cpp
    tests/sema_test.cpp
    tests/pkg_test.cpp
    tests/programs_test.cpp
    tests/abi_test.cpp
    tests/lsp_test.cpp
)
target_link_libraries(tlc_tests PRIVATE tlc_lib)
target_compile_definitions(tlc_tests PRIVATE
    "TLC_BIN=\"$<TARGET_FILE:tlc>\""
    "TLC_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

enable_testing()
add_test(NAME tlc_tests COMMAND tlc_tests)
