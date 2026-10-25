# atomic

[Index](../README.md) · [Previous: thread](thread.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`import "std/atomic"` loads `lib/tlc/std/atomic`. The package is ordinary tlc. A method calls [`builtin.atomic_load`](../builtin/atomic.md), `builtin.atomic_store`, `builtin.atomic_rmw`, or `builtin.atomic_cmpxchg`. The compiler replaces that call with one atomic instruction. The method call still records a trace frame.

`Int8`, `Int16`, `Int32`, `Int64`, `Uint8`, `Uint16`, `Uint32`, and `Uint64` each hold one integer. `Bool` holds a `u8` that is `0` or `1`. `Ptr[T]` holds a `*T`. The integer field and the `Bool` field are not exported. A file-scope variable starts at `0`. A local integer starts uninitialized until a store. `Ptr[T].value` is exported, because a pointer field cannot be left out: `atomic.Ptr[i32]{.value = &slot}`. `var p atomic.Ptr[i32]` is `type mismatch`.

`load` and `store` use `seq_cst`. `swap`, `add`, `sub`, `and`, `or`, and `xor` do too. Each of those returns the previous value. `load_order` and `store_order`, and the `_order` forms of the updates, take `order comptime i32`. A `var` is `type mismatch`. `cmpxchg(expected, desired)` is a strong compare-exchange. The success order is `seq_cst` and the failure order is `relaxed`. The results are the previous value and whether the store happened. `Bool` and `Ptr[T]` have `load`, `store`, `swap`, and `cmpxchg`. They do not have `add`.

The orders are `relaxed` `0`, `acquire` `1`, `release` `2`, `acq_rel` `3`, and `seq_cst` `4`. There is no `consume`. The update operations are `xchg` `0`, `add` `1`, `sub` `2`, `and` `3`, `or` `4`, and `xor` `5`. An order the operation does not allow, passed to an `_order` method, panics with `type mismatch`. A direct builtin call reports that order while compiling. A normal store of an `i32` is not atomic.

```c
import "std/atomic";

var n atomic.Int32;

func main() i32 {
    n.store(42);
    return n.add(1);
}
```

```asm
main:
subq $24, %rsp                   # slot for the calls
# trace frame for the call, then the matching pop
leaq n(%rip), %rdi               # &n
movl $42, %esi                   # 42
callq atomic.Int32.store
# trace frame for the call, then the matching pop
leaq n(%rip), %rdi               # &n
movl $1, %esi                    # 1
callq atomic.Int32.add
movl %eax, %ecx                  # previous value
movl %ecx, 4(%rsp)
movl 4(%rsp), %eax
addq $24, %rsp
retq

atomic.Int32.store:
movq %rdi, -16(%rsp)             # self
movl %esi, -4(%rsp)              # 42
movq -16(%rsp), %rcx
movl -4(%rsp), %eax
xchgl %eax, (%rcx)               # seq_cst store
retq

atomic.Int32.add:
movq %rdi, -16(%rsp)             # self
movl %esi, -4(%rsp)              # 1
movq -16(%rsp), %rcx
movl -4(%rsp), %eax
lock xaddl %eax, (%rcx)          # seq_cst add, previous value
retq
```

The program returns 42. `add` returns the value `n` held before the add. On this machine a `seq_cst` store is `xchgl` and a `seq_cst` add is `lock xaddl`. `atomic.Int32.load` is `movl (%rax), %eax` once the pointer is in `%rax`. A direct builtin call is the instruction itself, with no trace frame. That listing is on [atomic builtins](../builtin/atomic.md).

[Index](../README.md) · [Previous: thread](thread.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
