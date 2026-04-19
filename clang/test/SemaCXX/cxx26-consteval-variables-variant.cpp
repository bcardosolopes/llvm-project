// RUN: %clang_cc1 -std=c++26 -freflection -verify -verify-ignore-unexpected=note %s
// The variant example from P3603R0: Allowing consteval variables
// expected-no-diagnostics

namespace std {
    using size_t = decltype(sizeof(0));

    template <class T, size_t N>
    struct array {
        T _elems[N];

        constexpr auto operator[](size_t idx) const -> T const& {
            return _elems[idx];
        }
    };

    template <class T, class... Args>
    array(T, Args...) -> array<T, 1 + sizeof...(Args)>;
}

template <class T, class U>
struct Variant {
    union {
        T t;
        U u;
    };
    int index;

    constexpr Variant(T t) : t(t), index(0) { }
    constexpr Variant(U u) : u(u), index(1) { }

    template <int I> requires (I < 2)
    constexpr auto get() const -> auto const& {
        if constexpr (I == 0) return t;
        else if constexpr (I == 1) return u;
    }
};

template <class R, class F, class V0, class V1>
struct binary_vtable_impl {
    template <int I, int J>
    static constexpr auto visit(F&& f, V0 const& v0, V1 const& v1) -> R {
        return f(v0.template get<I>(), v1.template get<J>());
    }

    static constexpr auto get_array() {
        return std::array{
            std::array{
                &visit<0, 0>,
                &visit<0, 1>
            },
            std::array{
                &visit<1, 0>,
                &visit<1, 1>
            }
        };
    }

    static constexpr std::array fptrs = get_array();
};

template <class R, class F, class V0, class V1>
constexpr auto visit(F&& f, V0 const& v0, V1 const& v1) -> R {
    using Impl = binary_vtable_impl<R, F, V0, V1>;
    return Impl::fptrs[v0.index][v1.index]((F&&)f, v0, v1);
}

consteval auto func(const Variant<int, long>& v1, const Variant<int, long>& v2) {
    return visit<int>([](auto x, auto y) consteval { return x + y; }, v1, v2);
}

static_assert(func(Variant<int, long>{42}, Variant<int, long>{1729}) == 1771);
