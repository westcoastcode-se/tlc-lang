typedef struct { int a; float b; } FI;
typedef enum { EA, EB, EC } E;

static int inner_c(FI v) { return v.a * 100 + (int)v.b; }
int outer2(int (*f)(int (*)(FI))) { return f(inner_c); }
int call_fi(int (*f)(FI)) { FI v = { 40, 2.0f }; return f(v); }
int cover(FI v) { return v.a + 2 * (int)v.b; }
int call_enum(unsigned char (*f)(unsigned char)) { return f(2) + 10 * f(0); }
