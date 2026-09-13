One of the things that we've done on this branch over time is implemented a consteval-only value model (https://brevzin.github.io/cpp_proposals/4101_consteval_only_values/p4101r1.html) for how reflection works. But we still have consteval propagation — that is specializations of constexpr function templates or lambdas can become consteval if they do things that are consteval-only. This was a result of my paper from several years ago (https://brevzin.github.io/cpp_proposals/2564_consteval_patch/p2564r3.html).

There is another apporach that Daveed Vandevoorde suggested. Consider this example from the paper:

```cpp
consteval auto all_valid() -> bool {
    return std::ranges::none_of(
        types,
        [](std::meta::info i) {
            return std::meta::is_invalid(i); // #1
        }
    );
}
static_assert(all_valid());
```

Before consteval propagation, this is ill-formed. The call to is_invalid(i) isn't a constant expression and has to be. But after consteval propagation, we mark the lambda consteval, and then the specialization of none_of consteval, and everything works fine.

Daveed has another suggestion: what if, instead of propagating `consteval`, we do something different. If a `consteval` invocation isn't constant (as in the line `#1` above), we just sort of let it go. But we never codegen the function. If, after compiling the TU, we need a runtime definition of `is_invalid`, the program is ill-formed. Which in this case, we won't need it, because `all_valid()` itself is already `consteval`.

There are two potential issues with Daveed's design suggestion:

1. It's insufficient. There are examples that are well-formed because of consteval propagation that would be ill-formed without it.
2. It leads to poor diagnostics. A naive implementation would just lead to linker errors in which you have no idea _where_ the cause of the problem is.

I want to focus on the first. Because the second you could maybe hand-wave away as QoI. Are there examples of reflection code that _rely on_ consteval propagation that would fail with Daveed's model?
