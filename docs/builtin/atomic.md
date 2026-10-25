# atomic

[Index](../README.md) · [Previous: memeql and memcmp](mem.md) · [Next: builtin/env/consts](family.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`import "builtin"` loads the compiler package. These calls have bodies in `lib/tlc/builtin/builtin.tlc` so the types can be checked. Code generation replaces the call. The body does not run, and the call does not record a trace frame. [`atomic.Int32`](../std/atomic.md) and the other types in `std/atomic` call these.

`T` is `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, or a pointer. `f32`, `f64`, `bool`, and a struct are `not supported yet`. A value wider than 64 bits is `not supported yet`. `order` and `op` are comptime `i32` values. A `var` is `type mismatch`. An unknown constant is `type mismatch`.

`order` is `0` relaxed, `1` acquire, `2` release, `3` acq_rel, or `4` seq_cst. A load rejects release and acq_rel. A store rejects acquire and acq_rel. `op` is `0` xchg, `1` add, `2` sub, `3` and, `4` or, or `5` xor. `atomic_rmw` returns the previous value. On a pointer, an `op` other than xchg is `not supported yet`.

`atomic_cmpxchg` is a strong compare-exchange. The results are the previous value and whether the store happened. `success` is the order when the values match. `failure` is the order when they do not. `failure` cannot be release or acq_rel, and it cannot be stronger than `success`. `fence(0)` emits no instruction. Another accepted order emits one fence. `var f = builtin.fence`, `builtin.fence` without a call, and `defer builtin.fence(4)` are `not supported yet` and emit nothing. A direct call is replaced.

```c
import "builtin";

var n i32;

func main() i32 {
    builtin.atomic_store[i32](&n, 42, 4);
    return builtin.atomic_rmw[i32](&n, 1, 1, 0);
}
```

```asm
main:
movl $42, %eax                   # 42
xchgl %eax, n(%rip)              # seq_cst store
movl $1, %eax                    # 1
lock xaddl %eax, n(%rip)         # relaxed add, previous value
retq
```

The program returns 42, the value `n` held before the add. On this machine a `seq_cst` store is `xchgl` and a relaxed add is `lock xaddl`. A relaxed store is `movl`. `builtin.fence(4)` is `lock orl $0, -64(%rsp)`.

`atomic_load[T](p *T, order comptime i32)` returns `T`. `atomic_store[T](p *T, value T, order comptime i32)` has no result. `atomic_rmw[T](p *T, op comptime i32, value T, order comptime i32)` returns the previous `T`. `atomic_cmpxchg[T](p *T, expected T, desired T, success comptime i32, failure comptime i32)` returns `(T, bool)`. `fence(order comptime i32)` has no result.

[Index](../README.md) · [Previous: memeql and memcmp](mem.md) · [Next: builtin/env/consts](family.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
