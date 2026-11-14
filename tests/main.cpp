#include "driver/stack.hpp"

#include <iostream>

int run_cli_tests();
int run_diag_tests();
int run_lexer_tests();
int run_parser_tests();
int run_sema_tests();
int run_pkg_tests();
int run_program_tests();
int run_abi_tests();
int run_lsp_tests();
int run_battery_tests();

static int run_all() {
    int failed = 0;
    failed += run_cli_tests();
    failed += run_diag_tests();
    failed += run_lexer_tests();
    failed += run_parser_tests();
    failed += run_sema_tests();
    failed += run_pkg_tests();
    failed += run_program_tests();
    failed += run_abi_tests();
    failed += run_lsp_tests();
    failed += run_battery_tests();
    if (failed != 0) {
        std::cerr << failed << " check(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}

// The programs run in this process, so they get the compiler's stack too.
int main() { return tlc::run_on_large_stack(run_all); }
