# atomic

[Index](../README.md) · [Previous: panic](panic.md) · [Next: builtin/env/consts](family.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes the atomic builtins the compiler replaces with machine instructions.

## What the compiler replaces

`import "builtin"` loads the compiler package.

These calls are declarations in `lib/tlc/builtin/builtin.tlc`.

They have no body.

Code generation replaces the call.

The call does not record a trace frame.

[`atomic.Int32`](../std/atomic.md) and the other types in `std/atomic` call these.

## Types, orders, and operations

`T` is `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, a pointer, or a struct whose size is 1, 2, 4, or 8 bytes and whose alignment is at least that size.

`atomic_load`, `atomic_store`, `atomic_rmw`, and `atomic_cmpxchg` also take `f32`, `f64`, and `bool`.

On a float, add and sub are floating operations. `and`, `or`, `xor`, and `atomic_cmpxchg` use the bits.

A struct load, store, exchange, or compare-exchange uses the bits of the whole value. `add`, `sub`, `and`, `or`, and `xor` on a struct are `not supported yet`.

A struct whose size is not 1, 2, 4, or 8 bytes, or whose alignment is lower than that size, is `not supported yet`. Two `i32` fields in a plain struct are 8 bytes aligned to 4, so that struct is `not supported yet`. `struct(align=8)` with those fields is the 8-byte value.

A value wider than 64 bits is `not supported yet`.

`order` and `op` are comptime `i32` values.

A `var` is `type mismatch`.

An unknown constant is `type mismatch`.

`order` is `0` relaxed, `1` acquire, `2` release, `3` acq_rel, or `4` seq_cst.

[`atomic.Order`](../std/atomic.md) names those values.

This call still takes the integer.

A load rejects release and acq_rel.

A store rejects acquire and acq_rel.

`op` is `0` xchg, `1` add, `2` sub, `3` and, `4` or, or `5` xor.

`atomic.Op` names those values.

`atomic_rmw` returns the previous value.

On a pointer, an `op` other than xchg is `not supported yet`. On a struct, an `op` other than xchg is `not supported yet`.

## Compare-exchange and fences

`atomic_cmpxchg` is a strong compare-exchange.

The results are the previous value and whether the store happened.

`success` is the order when the values match.

`failure` is the order when they do not.

`failure` cannot be release or acq_rel, and it cannot be stronger than `success`.

`fence(0)` emits no instruction.

Another accepted order emits one fence.

`var f = builtin.fence` names that fence. `f(4)` is the same fence as `builtin.fence(4)`. `defer builtin.fence(4)` runs the fence when the block is left. `builtin.fence` without a call is `not supported yet`.

A direct call is replaced.

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

## What this program returns

The program returns 42, the value `n` held before the add.

On this machine a `seq_cst` store is `xchgl` and a relaxed add is `lock xaddl`.

A relaxed store is `movl`.

A `seq_cst` store of an `f32` is `xchgl` of those bits. A relaxed load of an `f32` is `movl`.

A relaxed store of a `bool` is `movb`. A `seq_cst` load of a `bool` is `movb`. The value is one byte.

A relaxed add of an `f32` is `addss` and `lock cmpxchgl`. A relaxed exchange of a `bool` is `xchgb`.

A `seq_cst` compare-exchange of an `f32` is `lock cmpxchgl` of those bits.

`builtin.fence(4)` is `lock orl $0, -64(%rsp)`.

## Signatures

`atomic_load[T](p *T, comptime order i32)` returns `T`.

`atomic_store[T](p *T, value T, comptime order i32)` has no result.

`atomic_rmw[T](p *T, comptime op i32, value T, comptime order i32)` returns the previous `T`.

`atomic_cmpxchg[T](p *T, expected T, desired T, comptime success i32, comptime failure i32)` returns `(T, bool)`.

`fence(comptime order i32)` has no result.

[Index](../README.md) · [Previous: panic](panic.md) · [Next: builtin/env/consts](family.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
