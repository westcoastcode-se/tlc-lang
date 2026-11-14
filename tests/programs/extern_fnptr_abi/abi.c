typedef struct { int a; float b; } FI;
typedef struct { long a, b, c; } Big;
typedef union { int i; float f; } U;
typedef struct { int (*run)(FI); Big (*make)(int); } Ops;

static int take_fi(FI v) { return v.a + (int)v.b; }
static int take_big(Big v) { return (int)(v.a + v.b + v.c); }
static FI make_fi(int n) { FI r = { n, 2.5f }; return r; }
static Big make_big(int n) { Big r = { n, 1, 1 }; return r; }
static U make_u(int n) { U u; u.i = n; return u; }
static int take_narrow(signed char c, unsigned short s, _Bool b) { return c + s + b; }
static int run_c(FI v) { return v.a * 2 + (int)v.b; }

int (*get_take_fi(void))(FI) { return take_fi; }
int (*get_take_big(void))(Big) { return take_big; }
FI (*get_make_fi(void))(int) { return make_fi; }
Big (*get_make_big(void))(int) { return make_big; }
U (*get_make_u(void))(int) { return make_u; }
int (*get_take_narrow(void))(signed char, unsigned short, _Bool) { return take_narrow; }

int call_fi(int (*f)(FI)) { FI v = { 40, 2.0f }; return f(v); }
int call_big(int (*f)(Big)) { Big v = { 40, 1, 1 }; return f(v); }
long call_make_big(Big (*f)(int)) { return f(7).a + f(7).c; }
float call_make_fi(FI (*f)(int)) { return f(1).b; }
int call_narrow(int (*f)(signed char, unsigned short, _Bool)) { return f(-2, 40, 1); }
/* A narrow result is extended by the callee. Read it as an int with every bit. */
int call_narrow_ret(signed char (*f)(int)) { int (*wide)(int) = (int (*)(int))f; return wide(-5); }

void fill_ops(Ops *ops) { ops->run = run_c; }
int use_ops(Ops *ops) { FI v = { 20, 2.0f }; return ops->run(v) + (int)ops->make(5).a; }
