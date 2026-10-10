#include <iostream>

int run_cli_tests();
int run_diag_tests();
int run_lexer_tests();
int run_parser_tests();
int run_sema_tests();
int run_pkg_tests();
int run_program_tests();

int main() {
    int failed = 0;
    failed += run_cli_tests();
    failed += run_diag_tests();
    failed += run_lexer_tests();
    failed += run_parser_tests();
    failed += run_sema_tests();
    failed += run_pkg_tests();
    failed += run_program_tests();
    if (failed != 0) {
        std::cerr << failed << " check(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
