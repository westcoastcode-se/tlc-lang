# thread

[Index](../README.md) · [Previous: seq](seq.md) · [Next: atomic](atomic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes threads, mutexes, and the other types in `std/thread`.

## Starting a thread

`import "std/thread"` loads `lib/tlc/std/thread`.

The package is ordinary tlc.

On unix it calls the pthread functions and the package link file adds `-lpthread`.

On windows each of those functions calls `builtin.panic("not supported yet")`.

`thread.spawn` takes `func (*void) *void` and a pointer, and starts that function on a new thread.

The pointer is the function's argument.

`thread.join` waits for the thread and returns the pointer the function returned.

The thread's writes happen before `join` reads them.

`thread.detach` detaches the thread.

`join` and `detach` are each called once.

`thread.yield` calls `sched_yield`.

`thread.id` returns the calling thread's id.

A failed pthread call panics with `thread`.

An inner function that uses a local or a parameter from an enclosing function is `not supported yet`.

The entry is a file-scope function.

```c
import "std/thread";

var slot i32;

func work(arg *void) *void {
    var p = unsafe *i32(arg);
    *p = 42;
    return arg;
}

func main() i32 {
    var t = thread.spawn(work, *void(&slot));
    thread.join(t);
    return slot;
}
```

```asm
main:
subq $56, %rsp                   # slot for the calls
# trace frame for the call, then the matching pop
leaq main.work(%rip), %rdi       # work
leaq slot(%rip), %rsi            # &slot
callq thread.spawn
movq %rax, 48(%rsp)              # t
movq 48(%rsp), %rax
movq %rax, 16(%rsp)
# trace frame for the call, then the matching pop
movq 16(%rsp), %rdi              # t
callq thread.join
movl slot(%rip), %eax            # slot
addq $56, %rsp
retq

main.work:
movq %rdi, -16(%rsp)             # arg
movq -16(%rsp), %rax
movq %rax, -8(%rsp)              # p
movq -8(%rsp), %rax
movl $42, (%rax)                 # *p = 42
movq -16(%rsp), %rax             # return arg
retq
```

The program returns 42.

## Mutex, condition, and once

`thread.Mutex` holds one pointer, `raw ?*void`.

The field value is `null`, so `var lock thread.Mutex` starts null, at file scope and in a function.

`lock.init` allocates 128 bytes and calls `pthread_mutex_init`.

That size covers the unix mutex types this package uses.

`malloc` aligns the block to 16.

A second `init` panics with `thread`.

`lock`, `unlock`, and `destroy` on a null mutex panic with `null pointer`.

`destroy` calls `pthread_mutex_destroy` and `free`, then stores `null`.

A copy of a `Mutex` is a copy of the pointer.

`thread.Cond` uses that same 128-byte block.

`thread.RwLock` allocates 256 bytes, which covers a macOS `pthread_rwlock_t` of 200 bytes and the 56-byte glibc and musl locks.

`wait` takes `*Mutex`.

`rlock` and `wlock` share `pthread_rwlock_unlock` for `runlock` and `wunlock`.

`thread.Once` is one `i32`.

Zero means the function has not run, at file scope and in a function.

`call` runs a `func () void` once.

That function takes no argument.

[Index](../README.md) · [Previous: seq](seq.md) · [Next: atomic](atomic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
