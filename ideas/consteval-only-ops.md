I want to investigate changing the model for consteval-only values. The current implementation has reflections, pointers and references to consteval variables, and pointers and references to consteval functions as being consteval-only values — and those escalate to prevent any actual reflection from escaping to runtime.

Davis is proposing a new model, which we'll call the consteval-only operations model. This has the following properties:

* all of the interesting operations on `meta::info` are already `consteval`, but `==` and `!=` additionally need to be treated as if they were `consteval` functions as well.
* `meta::info` becomes a valid "permitted result of a constant expression" — which also means that it's allowed to persist to runtime. However, the translation to runtime of `meta::info` will be an empty struct.

That is, for this declaration:

```cpp
meta::info r = ^^int;
bool b = r == ^^int;
```

In the consteval-only type model (p2996), this is ill-formed because the declaration of `r` must be `consteval`.

In the consteval-only value model (this branch), this is ill-formed because `^^int` is a consteval-only value so the initialization of `r` is, so it must be `consteval`.

In the consteval-only _operations_ model (new), this is valid. `^^int` is a permitted result of a constant expression, `r` persists to runtime, but it has no state (since reflections at runtime are meaningless). But the declaration of `b` is ill-formed, because `r == ^^int` must be a constant expression in this model and cannot be.

Note that we _keep_ the existing consteval-only value machinery, in the sense that a pointer/reference to consteval function/variable is consteval-only, and a constexpr variable whose initialization is a consteval-only value becomes a consteval variable. It's just that reflections _themselves_ are no longer consteval-only.

Other design decisions:

1. `info` should be an empty type at runtime. So `is_empty_v<info>` is `true`, `sizeof(info) == 1`, etc.
2. We can add a bespoke diagnostic for the `r == ^^int` case for clarity, doesn't hurt I guess.
3. We can't quite exactly reuse the escalation machinery for `==` and `!=` since those aren't declared functions, but we should be able to do a simple bespoke escalation for those operators?
