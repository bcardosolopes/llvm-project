// RUN: %clang_cc1 -std=c++20 -emit-llvm -Wno-unused-value -o - %s | FileCheck %s
//
// The generic lambda's call operator (instantiated with <int, int>) is the
// declaration whose CodeGen used to crash; check it is actually emitted, so the
// test can't silently rot into a no-op if instantiation stops being forced.
// CHECK: define {{.*}}cqIiiLi1E{{.*}}clIiiE
//
// Regression test for a CodeGen crash (an assertion
// `E->getType()->isPointerType()...` in EmitPointerWithAlignment, or a SIGSEGV
// in VisitCastExpr in release builds).
//
// A generic lambda whose body references a non-type template parameter of the
// *enclosing* function template (whose type itself depends on another parameter
// of that template) is type-erased into a std::function-like wrapper. Erasing
// it forces return-type deduction -- hence body instantiation -- of the
// lambda's call-operator specialization. Re-running the substitution over the
// already-resolved reference to the enclosing parameter recomputed its
// still-dependent type and regressed the node back to a dependent cast, which
// then reached CodeGen and crashed. The reduced structure below is what is
// needed to trigger the eager instantiation, so the names are kept terse.

namespace {
template <int b> struct c {
  static const int d = b;
};
template <bool e> using f = c<e>;
template <bool, class a = void> using g = a;
template <class aa> struct j {
  using h = aa;
};
template <bool, class aa, class> using i = j<aa>::h;
template <class...> class m;
template <class> struct D {
  template <int> using l = int;
};
template <int, int ai>
using aj = __make_integer_seq<D, int, ai>::template l<ai>;
template <int ak, int ai = 0> struct o {
  typedef aj<ak, ai> h;
};
template <class...> using am = c<true>;
template <class... an> am<g<an::d>...> ao(int);
template <class... an> using ap = decltype(ao<an...>(0));
int aq;
template <class ar, class... as> decltype(ar()(as()...)) at(ar p, as... q) {
  p(q...);
}
template <class, class ar, class... as> struct r {
  template <class ax, class... ay> static decltype(at(ax(), ay()...)) az();
  using a = decltype(az<ar, as...>);
  using h = i<f<__is_same(int, void)>::d, c<true>, c<false>>;
  static const bool d = h::d;
};
template <class ar, class... as> using bb = r<void, ar, as...>;
struct s {
  template <class... as> static void bd(as...) { at(as()...); }
};
template <class a> struct be {
  using bf = a;
  be(int, m<>, int);
};
template <class bg> class t : be<bg>, be<int> {
public:
  using bj = be<bg>;
  using bk = be;
  template <class... bl, class... bm>
  t(int p, m<bl...> q, m<bm...> p3)
      : bj(p, q, typename o<sizeof...(bl)>::h()),
        bk(p, p3, typename o<sizeof...(bm)>::h()) {}
  bj::bf bp();
};
template <class...> class m {};
m<> br();
template <int bs> struct I {
  using bv = int;
  constexpr bv bw() { return bs; }
  void operator[](bv) const;
};
template <class> class bx;
template <class, class, class> class bz;
template <class ar, class ca, class cb, class... cc>
class bz<ar, ca, cb(cc...)> {
  t<ar> cd;

public:
  bz(ar, ca) : cd(aq, br(), br()) {}
  void operator()() { s::bd(cd.bp(), cc()...); }
};
template <class, class, class> class ce;
template <class ar, class cf, class cb, class... cc>
class ce<ar, cf, cb(cc...)> {
  bz<ar, cf, cb(cc...)> cd;

public:
  ce(ar p, cf q) : cd(p, q) {}
  virtual cb operator()(cc &&...);
};
template <class ar, class cf, class cb, class... cc>
cb ce<ar, cf, cb(cc...)>::operator()(cc &&...) {
  cd();
}
template <class> class ch;
template <class cb, class... cc> class ch<cb(cc...)> {
public:
  template <class ar, class cf> ch(ar p, cf) { ce<ar, cf, cb(cc...)>(p, cf()); }
  template <class ar> ch(ar) : ch(ar(), int()) {}
};
template <class cb, class... cc> class bx<cb(cc...)> {
  ch<cb(cc...)> cd;
  template <class ar, bool = ap<bb<ar, cc...>>::d> struct u {
    static const bool d = f<__is_same(int, void)>::d;
  };
  template <class ar> using ck = g<u<ar>::d>;

public:
  template <class ar, class = ck<ar>> bx(ar);
};
template <class cb, class... cc>
template <class ar, class>
bx<cb(cc...)>::bx(ar p) : cd(p) {}
} // namespace
void cn(bx<void(int, int)>) {}
namespace {
I<1> co;
template <typename, typename, int n, I<n> cp, int> void cq() {
  cn([](auto, auto) {
    for (int k;;)
      cp[k];
  });
}
template <typename, typename> void cq() { cq<int, int, co.bw(), co, 1>; }
} // namespace
void cq() { cq<int, int>; }
