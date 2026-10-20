#include "driver/driver.hpp"

int main(int argc, char **argv) {
    return tlc::execute(tlc::parse_args(argc, argv));
}
