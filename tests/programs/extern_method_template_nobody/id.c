int tlc_point_id_spec_i32(int *self, int value) __asm__("main.Point.id.spec.i32");

int tlc_point_id_spec_i32(int *self, int value) {
    (void)self;
    return value + 41;
}
