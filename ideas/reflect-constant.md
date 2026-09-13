Extend support for non-type (constant) template parameters by allowing types to customize `std::meta::reflect_constant`.

## Opting into `reflect_constant`

A type `C` can opt-in by writing a `public`, `consteval` non-static member function that has to have one of the following signatures — same as the shapes from the comparison rules that you can default:

```cpp
struct C {
    consteval auto reflect_constant() const -> std::meta::info;
    consteval auto reflect_constant() const& -> std::meta::info;
    consteval auto reflect_constant(this C const&) -> std::meta::info;
    consteval auto reflect_constant(this C) -> std::meta::info;
};
```

This function's job is to return a reference representing the template parameter object that is template-argument-equivalent to the object parameter. That is — this object _is_ the object that the language will use as _the_ template parameter object.

When initializing a constant template parameter of type `C`:

```cpp
template <C c> void f();
```

`f<x>()` will evaluate not by copying `x` to initialize a new object of type `C`, but rather the template parameter `c` will be an lvalue that refers to the object denoted by `x.reflect_constant()`. `f<x>` and `f<y>` are the same specialization if and only if `x.reflect_constant()` and `y.reflect_constant()` are reflections representing the same object. The argument is mangled as if the parameter were a reference to the returned entity.

`std::meta::reflect_constant(x)` itself will call `x.reflect_constant()` if that customization point exists. Likewise `std::meta::is_structural(ty)` will also include customized types.

If `C` has any `mutable` (direct or indirect) data members, `C` is still non-structural and cannot be used as a non-type template parameter, no matter the presence of a customization point.

For either use (either `std::meta::reflect_constant` or initializing a constant template parameter), the customization point must return a `std::meta::info` that represents an object (or subobject) or variable. Letting `O` be that object or the object declared by that variable, all of the following must hold:

* `O` has static storage duration,
* `O` is usable in constant expressions,
* `O` has linkage, and
* `O` has type `C const`.

If any of those conditions are violated, it is considered a substitution failure in the immediate context. This branch doesn't have constexpr exceptions, but the behavior would be that `std::meta::reflect_constant()` would throw an exception. If `x.reflect_constant()` itself is not a constant expression, then that's a hard error.

If `O` has internal linkage, the specialization is TU-local (similar to today's rules for internal-linkage pointers). Note that because the object has to have the same type, we're effectively ruling out inheriting a customization point — since `Base`'s implementation must return an object of type `Base const`, that'll never match `Derived const`.

Note that:

* `reflect_constant` must be idempotent. If `p` is a template parameter object, then `p.reflect_constant()` should return a reflection representing `p`.
* `reflect_constant` needs to be deterministic: same value in, same object out. It need not be 1-1 (as in the `Frac` example below), and the meaning of "same" is up to the class author, but it cannot produce different objects out.

The former will be checked by the implementation (in the same way that the current language performs an extra copy). Whenever we initialize a template parameter object (either by directly substituting into a template or via `std::meta::reflect_constant`), an extra call to `O.reflect_constant()` happens and we ensure that it's the same object. If it's a different object, it's a substitution failure.

## Defaulting

`reflect_constant` can also be defaulted, which means we have the same template-argument-equivalent/structural type rules as we have today, except that we allow private members:

```cpp
class D {
    int i;

public:
    consteval auto reflect_constant() const -> std::meta::info = default;
};
```

Using a `D` as a constant template parameter behaves the same as if `D::i` were actually public. `default`ing just opts into using all subobjects, regardless of their access. It can also be defaulted out of line.

Very many interesting and useful standard library types can simply have `reflect_constant` defaulted: `std::tuple`, `std::basic_string_view`, `std::span`, `std::optional`, `std::expected`, and `std::variant` come to mind.

Defaulting `reflect_constant` should follow the same design as defaulting the special member functions and comparisons: if all subobjects are structural (whether C++26 structural or otherwise opt into `reflect_constant`), then we do member-wise equivalence. Otherwise (if any subobject is not structural), this is defined as deleted.

The defaulted types keep the existing template parameter object machinery, applied to the normalized value.

`reflect_constant` can also be explicitly deleted, to opt out of constant template parameter support, even if it would otherwise exist.

```cpp
struct Nope {
    int i;
    consteval auto reflect_constant() const -> std::meta::info = delete;
};
```

Note that `Nope`'s opt-out is infectious — any type with a `Nope` subobject is not structural / usable as a non-type template parameter.

## Recursing

Let's build a simple example. I have a fraction type that, for some reason, I want to reduce to lowest terms when used as a template parameter. I can write that:

```cpp
struct Frac {
    int numer;
    int denom;

    template <int N, int D>
    static constexpr Frac f = Frac{N, D};

    consteval auto reflect_constant() const -> std::meta::info {
        int g = std::gcd(numer, denom);
        return substitute(
            ^^f,
            {std::meta::reflect_constant(numer / g), std::meta::reflect_constant(denom / g)}
        );
    }
};

template <auto F> void f();
```

Now, the call to `f<Frac{2, 4}>()` will have `F` refer to an object whose `numer` is `1` and whose `denom` is `2`.

But this process recurses. So if I have:

```cpp
struct Stuff {
    Frac frac;
    int x;
};
```

Then the call to `f<Stuff{{2, 4}, 6}>()` will have `F` refer to a `Stuff` whose `frac` member is `{1, 2}` and whose `x` is `6`. That is, implicitly, every subobject also undergoes this treatment. The process is that we are basically initializing a new `Stuff` object from the argument object `rhs` as:

```cpp
Stuff{
    .frac = [: std::meta::reflect_constant(rhs.frac) :],

    // for types that are C++26 structural (have no customization point anywhere)
    // we can simplify the implementation to just do .x = x
    .x = [: std::meta::reflect_constant(rhs.x) :],
};
```

Note that `std::meta::reflect_constant(Stuff{{2, 4}, 6})` should do the same thing — the resulting object will have a `frac` member that is `{1, 2}`. Note that this requires `Frac` to be constexpr-copyable. More generally, whenever this normalization happens for a subobject with customized `reflect_constant`, that type will have to be constexpr-copyable. If not, it's ill-formed a point of use (a substitution failure).

The example above only shows members, but recursion applies to all subobjects: arrays, base-class subobjects, etc. Note that _reference_ data members do not participate in normalization, with the exception of reference to string literal objects (see below). Unions normalize the active member, if any; the union is structural only if all alternatives are.

## String Literals

Related, but not strictly the same as, the above is what to do about string literals. Since `f<"hello">()` doesn't work today without the call side opting in to be able to handle string literals somehow.

However, there is a clear answer to how to address this use-case as well, that is related to the above process. This branch used to (but somehow lost — see the `brevzin/is-string-literal` branch) have `std::is_string_literal(p)` and `std::string_literal_from(p)`. The former simply identified a `char const*` as being a string literal (or subobject) and the latter would give you the beginning of the string literal (or `nullptr`). Note specifically a string literal, a pointer into a global constexpr array of char is not a string literal.

So for instance:

```cpp
consteval auto test() -> void {
    char const* p = "hello";
    char const* q = p + 1;

    assert(is_string_literal(p));
    assert(is_string_literal(q));
    assert(string_literal_from(q) == p);
}
```

Note that what `string_literal_from` returns is based on where the object actually came from:

```cpp
constexpr char const* a = "hello";
constexpr char const* b = "lo";
constexpr char const* c = a + 3;

static_assert(is_string_literal(a));
static_assert(is_string_literal(b));
static_assert(is_string_literal(c));

// b is its own string literal object, even if it's a suffix of a
// but c is a suffix of a by construction, which the existing language already tracks
static_assert(string_literal_from(b) == b);
static_assert(string_literal_from(c) == a);
```

The normalization process I described above is that every subobject of default-structural types is round-tripped through `reflect_constant` — we can perform the same process for string literals! If we have some `char const*` that is a string literal — which is to say that it is `S + I` where `S` is some "root" string literal and `I` is some non-negative integer (`0 ≤ I ≤ N` — the end boundary is allowed), then we round-trip the pointer through `define_static_string(S) + I`.

This allows using string literals (top-level or as subobjects) as constant template parameters and via `std::meta::reflect_constant`.

We are basically repealing the [temp.arg.nontype] ban on using string literals, globally. The ban exists precisely because we didn't have a way to define identity properly and could not enforce that `f<"hello">` was the same specialization in every TU. I am defining identity now.

Punting on character types other than `char` for now, so the above only applies to string literals that have type `char const[N]`.
