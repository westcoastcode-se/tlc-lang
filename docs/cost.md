# Assembly

[Index](README.md) · [Grammar](grammar.md)

The assembly of a TLC example comes immediately after that example. The listing shows what the example runs. It is not a section at the end of the page, and it is not a count of every form on the page.

Take the instructions from the object file `tlc --emit-obj` writes for `x86_64-pc-linux-gnu` with no `-O` flag. With no `-O` flag, `tlc` removes a store that is overwritten before it is read, or that is never read. It does not run the other LLVM optimizations. `-O0` keeps that store. `-O`, `-O1`, `-O2`, and `-O3` run LLVM's optimization pipeline, and a listing on a feature page does not apply to those builds.

Write AT&T syntax, one instruction a line. Drop addresses, raw bytes, and alignment `nop`. Leave out a path the example does not take. Write a source immediate in decimal. A comment on the line names the source step. A label in the listing names a branch from the object file. It is not a symbol the compiler emits.

A parameter is stored on entry: `movl` for an `i32`, `movq` for a pointer, `movss` or `movsd` for a float. A local with no initializer is uninitialized. Reading it is reported as `uninitialized`. There is no store for that local, and `-O0` does not add one. A file-scope variable with no initializer is zero. A struct field uses the value written on the field when it has one. With no `-O` flag, that store is removed when the value is not read.

`return 40 + 2` is folded while compiling:

```asm
movl $42, %eax    # 40 + 2
retq
```

## A call

A call records a trace frame, then calls. The frame is 24 bytes: a file, a line, a column, and the name of the callee. At most 32 frames are stored. The frame count, the 32 frames, and the text buffer are thread-local, so one thread does not see another's frames. `builtin.trace` and `builtin.va` do not push a frame. A `defer` runs its call later without a frame. A function with a trait parameter is specialized, and the call of that specialized function does not push a frame. Calls inside the specialized body still push frames. `malloc` and `free`, used by `new` and `delete`, do not push a frame.

On the path where fewer than 32 frames are already stored, this program is the listing below. The line and the column are the call. At 32 frames the push and the pop are skipped. The names `frame_count`, `trace`, `file`, `name`, and the branch labels stand in for the thread-local offsets and the string addresses in the object file. `frame_count` and `trace` are read and written through `%fs`.

```c
func noop() i32 {
    return 1;
}
func main() i32 {
    return noop();
}
```

```asm
subq  $24, %rsp                       # slot for the call
movl  %fs:frame_count, %ecx           # frames already stored
movl  %ecx, 16(%rsp)
xorl  %eax, %eax
cmpl  $32, %ecx                       # the trace keeps 32 frames
movb  %al, 23(%rsp)                   # this call has not pushed
jae   .no_push
movl  16(%rsp), %eax
movslq %eax, %rcx
leaq  (%rcx,%rcx,2), %rcx             # index times 3, then scaled by 8
leaq  file(%rip), %rdx
movq  %rdx, %fs:trace(,%rcx,8)        # file
movl  $5, %fs:trace+8(,%rcx,8)        # line of the call
movl  $12, %fs:trace+12(,%rcx,8)      # column of the call
leaq  name(%rip), %rdx
movq  %rdx, %fs:trace+16(,%rcx,8)     # callee name
incl  %eax
movl  %eax, %fs:frame_count
movb  $1, %al
movb  %al, 23(%rsp)                   # this call pushed
.no_push:
movb  23(%rsp), %al
movb  %al, 11(%rsp)
callq noop
movl  %eax, %ecx                      # result of noop
movb  11(%rsp), %al
movl  %ecx, 12(%rsp)
testb $1, %al                         # pop only a frame this call pushed
jne   .pop
.pop:
movl  %fs:frame_count, %eax
decl  %eax
movl  %eax, %fs:frame_count
movl  12(%rsp), %eax
addq  $24, %rsp
retq

noop:
movl  $1, %eax                        # return 1
retq
```

Another feature page keeps `callq` and the instructions that are that feature. It replaces the push and the pop with one comment that points here.

[Index](README.md) · [Grammar](grammar.md)
