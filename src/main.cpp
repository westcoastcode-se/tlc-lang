#include "driver/driver.hpp"
#include "driver/stack.hpp"

int main(int argc, char **argv) {
    return tlc::run_on_large_stack([&] { return tlc::execute(tlc::parse_args(argc, argv)); });
}
