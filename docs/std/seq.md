# seq

[Index](../README.md) · [Previous: string](string.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`import "std/seq"` loads `lib/tlc/std/seq`. The package declares one trait, `seq.Seq[T]`. The compiler treats that `extern` trait as the iteration trait because the package's `link` file names `libseq.a`. It reads `len`, `at`, and `ref` from the declaration. A file names the trait by importing the package. A copy of the source that does not link that library is an ordinary trait.

`len` has no parameter after the receiver and returns `i32`. `at` takes one `i32` and returns `T`. `ref` takes one `i32` and returns `*T`. A struct writes those names. `len` and `at` take the value. `ref` takes `self *this`, because the pointer refers to the struct. A value parameter `seq.Seq[T]` checks `len` and `at`. The pointer receiver on `ref` does not reject that value. A pointer parameter `*seq.Seq[T]` checks `ref` as well. A field, a further method, or a default on this trait is reported as `not supported yet`.

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

The program returns 42. `Bag` has no `ref`. The value parameter does not require it. `items.len()` and `items.at(0)` are direct calls of `Bag.len` and `Bag.at`. A missing `len` or `at` is reported as `undeclared identifier`. A pointer receiver on `len` or `at` is reported as `type mismatch` when the parameter is a value. `*seq.Seq[i32]` also requires `func Bag::ref(self *this, i i32) *i32`. A missing `ref` on that pointer parameter is reported as `undeclared identifier`. An array passed as `seq.Seq[i32]` is reported as `type mismatch`.

The trait has no size and no symbol. `var s seq.Seq[i32]`, a result, an array, a slice, and a field of the trait are reported as `trait has no size`. `libseq.a` is the archive the link file names. The trait calls no symbol in that archive.

## Cost

Counts are the instructions that run, from `tlc --emit-obj` on `x86_64-pc-linux-gnu`. Alignment `nop` is left out. Checking that a struct satisfies `seq.Seq[T]` adds no instructions. `libseq.a` defines no symbol these calls use.

The example returns 42. `main` executes 6 instructions: it stores 40, passes that `i32`, and calls `main.use.spec.Bag`. That call does not push a trace frame. `use` executes 78 instructions when fewer than 32 frames are stored. Those 78 include one trace frame for `len`, one for `at`, both calls, and the add. `Bag.len`, returning 2, is 2 instructions and records no frame of its own. `Bag.at`, adding the stored `i32` and the index, is 5.

[Index](../README.md) · [Previous: string](string.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
