# seq

[Index](../README.md) · [Previous: string](string.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`import "std/seq"` loads `lib/tlc/std/seq`. The package declares `seq.Seq[T]` and `ptr_of[T]`. The compiler treats the `extern` trait `Seq` in that directory as the iteration trait. It reads `len` and `at` from that declaration. A file names the trait by importing the package. A copy of the source outside that directory is an ordinary trait. `ptr_of[T]` has `len` and `ref`. `for (var item : ptr_of(items))` selects it without an import. A missing `ref` on that value is `type mismatch`.

`len` has no parameter after the receiver and returns `i32`. `at` takes one `i32` and returns `T`. A struct writes those names, and both take the value. `seq.Seq[T]` and `*seq.Seq[T]` check `len` and `at`. They do not have `ref`. A pointer to an element is `for (var item : ptr_of(items))`, and that value's `ref` takes `self *this` and one `i32` and returns `*T`. A field, a further method, or a default on `Seq` is reported as `not supported yet`.

```c
import "std/seq";

type Bag struct {
    n i32;
};

func Bag::len(self this) i32 {
    return 2;
}

func Bag::at(self this, i i32) i32 {
    return self.n + i;
}

func use(items seq.Seq[i32]) i32 {
    return items.len() + items.at(0);
}

func main() i32 {
    var bag Bag;
    bag.n = 40;
    return use(bag);
}
```

The program returns 42. `items.len()` and `items.at(0)` are direct calls of `Bag.len` and `Bag.at`. A missing `len` or `at` is reported as `undeclared identifier`. A pointer receiver on `len` or `at` is reported as `type mismatch` when the parameter is a value. `*seq.Seq[i32]` checks those same two methods. `Bag::ref` is not part of that check. `for (var item : ptr_of(bag))` reads `ref`.

`[N]T` and `[]T` satisfy `seq.Seq[E]`, where `E` is the element an index reads. That includes a parameter written `[N]T`, a string `[]const u8`, `[N][M]T`, and `[N][]T`. The element of `[N][M]T` is `[M]T`. `items.len()` is `.len`. `items.at(i)` is the index. `for (var item : ptr_of(items))` binds `&items[i]`. `[4]i32` and `[8]i32` are different copies. Every `[]i32` is one copy, and a string passed as `seq.Seq[u8]` shares the copy of `[]u8`. A slice alias such as `type Row []i32` satisfies the trait only when it defines `len` and `at`. A missing method is reported as `undeclared identifier`. A pointer receiver, or a result that is not `E`, is reported as `type mismatch`. A pointer to an array or a slice, `*[N]T` or `*[]T`, is reported as `type mismatch`, and so is a number. Passing an array where `*seq.Seq[E]` is written does not take its address. A struct satisfies the trait only by its own methods. An array or a slice satisfies no other trait. An array passed to a copy of the trait outside `lib/tlc/std/seq` is reported as `type mismatch`.

```c
import "std/seq";

func use(items seq.Seq[i32]) i32 {
    return items.len() + items.at(0);
}

func main() i32 {
    var items [4]i32;
    items[0] = 38;
    return use(items);
}
```

This program returns 42.

The trait has no size and no symbol. `var s seq.Seq[i32]`, a result, an array, a slice, and a field of the trait are reported as `trait has no size`.

## Cost

Counts are the instructions that run, from `tlc --emit-obj` on `x86_64-pc-linux-gnu`. Alignment `nop` is left out. Checking that a struct satisfies `seq.Seq[T]` adds no instructions.

The struct example returns 42. `main` executes 6 instructions: it stores 40, passes that `i32`, and calls `main.use.spec.Bag`. That call does not push a trace frame. `use` executes 78 instructions when fewer than 32 frames are stored. Those 78 include one trace frame for `len`, one for `at`, both calls, and the add. `Bag.len`, returning 2, is 2 instructions and records no frame of its own. `Bag.at`, adding the stored `i32` and the index, is 5.

The array example returns 42. `main` executes 6 instructions: it stores 38, passes the address of the first element, and calls `main.use.spec.4.i32`. That call does not push a trace frame. The specialized function executes 5 instructions: the pointer store, one reload, one load of the element, the add of 4, and `retq`. `len` is that constant 4. The function pushes no trace frame. `[8]i32` is the separate copy `main.use.spec.8.i32`. `[]i32` is `main.use.spec.slice.i32`.

[Index](../README.md) · [Previous: string](string.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
