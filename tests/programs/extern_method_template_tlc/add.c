int tlc_point_add_spec_i32(int *self, int n) __asm__("main.Point.add.spec.i32");

int tlc_point_add_spec_i32(int *self, int n) {
    return *self + n;
}
