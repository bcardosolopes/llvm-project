New idea: consteval variables are allowed to have persistent allocation:

```cpp
constexpr int const* a = new int(1); // error

consteval int const* b = new int(2); // ok
static_assert(*b == 2); // ok

constexpr std::vector<int> c = {1, 2, 3}; // error

consteval std::vector<int> d = {4, 5, 6}; // ok
static_assert(d[0] == 4); // ok
static_assert(d.size() == 3); // ok
```

Basically, today a constant expression cannot leak an allocaiton, but we can say that with a `consteval` variable, this is okay. With the additional behavior that:

* consteval allocation has static storage for the remainder of the compilation
* addressing consteval allocation is a consteval-only value. So `b` and `d.data()` cannot persist to runtime.
* this storage is immutable — 
