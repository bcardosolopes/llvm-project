Zach Laine wrote a standards proposal (P2727, https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2727r4.html) proposing a utility called `std::iterator_interface`, standardizing his own Boost library called STL interfaces (https://github.com/boostorg/stl_interfaces, https://www.boost.org/doc/libs/1_87_0/doc/html/stl_interfaces.html).

The goal here was to be able to make it much easier to implement a C++20 iterator and avoid the boilerplate. One example from the paper is:

```cpp
struct repeated_chars_iterator :
    std::iterator_interface<std::random_access_iterator_tag, char, char>
{
    constexpr repeated_chars_iterator() :
        first_(nullptr),
        size_(0),
        n_(0)
    {}
    constexpr repeated_chars_iterator(char const * first,
                                      difference_type size,
                                      difference_type n) :
        first_(first),
        size_(size),
        n_(n)
    {}

    constexpr char operator*() const
    {
        return first_[n_ % size_];
    }

    constexpr repeated_chars_iterator& operator+=(std::ptrdiff_t i)
    {
        n_ += i;
        return *this;
    }

    constexpr auto operator-(repeated_chars_iterator other) const
    {
        return n_ - other.n_;
    }

private:
    char const * first_;
    difference_type size_;
    difference_type n_;
};
```

Now, we live in a world here where we have code injection. So it'd be nice to be able to do this with code injection somehow, rather than inheritence — which adds significant complexity to the design in a way that also makes it harder to understand the logic flow. Basically, the implementation here is (or should be) simple: we just go through every operation and conditionally add some function (or not) based on the presence of some other function.

Now, in this branch, `members_of` can give you the members of a class as its being defined — but that only lets me check if `operator+=` literally exists, not if it's actually invocable. So that might be the wrong approach. We probably actually want to be able to use `requires` expressions, so that I can actually check `requires { it += ptrdiff_t(0); }` and go from there. But maybe that's difficult to do for a class still being complete?

So I think either the shape needs to be something like this:

```cpp
struct repeated_chars_iterator
{
    constexpr repeated_chars_iterator() :
        first_(nullptr),
        size_(0),
        n_(0)
    {}
    constexpr repeated_chars_iterator(char const * first,
                                      difference_type size,
                                      difference_type n) :
        first_(first),
        size_(size),
        n_(n)
    {}

    constexpr char operator*() const
    {
        return first_[n_ % size_];
    }

    constexpr repeated_chars_iterator& operator+=(std::ptrdiff_t i)
    {
        n_ += i;
        return *this;
    }

    constexpr auto operator-(repeated_chars_iterator other) const
    {
        return n_ - other.n_;
    }

    consteval
    {
        the_rest_of_the_owl({
            .value_type=^^char,
            .iterator_concept=^^std::random_access_iterator_tag,
        });
    }

private:
    char const * first_;
    difference_type size_;
    difference_type n_;
};
```

Or we'd have to pursue metaclasses (https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0707r5.pdf) such that this becomes shaped more like:

```cpp
struct(iterator_interface({...})) repeated_chars_iterator {
    // same body as before
};
```

The latter shape has some benefit just because we see the iterators up front, and the metaclass design probably helps out with the question of incompleteness — since we will have a complete type that we can check things about, although cloning the implementation (especially in the presence of templates!) adds a whole layer of complexity.
