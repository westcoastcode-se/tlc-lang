# Cost

[Index](README.md) · [Grammar](grammar.md)

With no `-O` flag, `tlc` removes a store that is overwritten before it is read, or that is never read. It does not run the other LLVM optimizations. `-O0` keeps that store. `-O`, `-O1`, `-O2`, and `-O3` run LLVM's optimization pipeline, and a count on a feature page does not apply to those builds. A count is the number of x86-64 instructions that run, taken from the object file `tlc --emit-obj` writes for `x86_64-pc-linux-gnu` with no `-O` flag. An instruction on a branch the running path does not reach is left out. Alignment `nop` is left out. `callq` is one instruction. The instructions inside the callee are extra and are stated with the callee.

A parameter is stored on entry: one `movl` for an `i32`, one `movq` for a pointer, one `movss` or `movsd` for a float. `var n i32 = 1` stores 1. A local with no initializer is zero. With no `-O` flag, that store is removed when the value is not read, so the declaration adds no instruction. Reading the zero keeps the store. `-O0` keeps the store.

## A call

A call records a trace frame, then calls. The frame is 24 bytes: a file, a line, a column, and the name of the callee. At most 32 frames are stored.

On the path where fewer than 32 frames are already stored, `return noop()` executes 36 instructions in the caller. The frame is pushed, `callq` runs, and the frame is popped. `noop`, returning the constant 1, is 2 more instructions. Two constant `i32` arguments add 2 `movl` into the argument registers. When 32 frames are already stored, the push is skipped and the pop is skipped.

`builtin.trace` and `builtin.va` do not push a frame. A `defer` runs its call later without a frame. A function with a trait parameter is specialized, and the call of that specialized function does not push a frame. Calls inside the specialized body still push frames. `malloc` and `free`, used by `new` and `delete`, do not push a frame.

`return 40 + 2` is folded while compiling. `main` executes 2 instructions, `movl $42, %eax` and `retq`.
