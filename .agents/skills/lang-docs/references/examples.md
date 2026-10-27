# Good and Bad Documentation Examples

## Bad: Abstract and no example

The language supports higher-order functions. Functions can be passed as arguments and returned from other functions. This enables functional programming patterns.

## Good: Concrete and example-driven

Functions are first-class values. You can pass a function as an argument or return one from another function.

```lang
func apply(f fn(i32) i32, x i32) i32 {
    return f(x);
}

func double(n i32) i32 { 
    return n * 2;
}

const result = apply(double, 21);
// result == 42
```

## Bad: Condescending and vague

Simply use the `map` function to transform your list. It's easy.

## Good: Clear and complete

Use `map` to create a new list by applying a function to every element.

```lang
const numbers = [_]i32{1, 2, 3};
const doubled = map(numbers, func (n) { return n * 2 });
// doubled == [_]i32{2, 4, 6}
```

`map` does not modify the original list.

## Bad: Incomplete snippet

```lang
result = compute(x)
```

## Good: Complete example with context

```lang
func main() {
    var x = 10;
    var result = compute(x);
    print(result);
}

func compute(n i32) i32 {
    return n * 2
}
```

Output:
```
20
```
