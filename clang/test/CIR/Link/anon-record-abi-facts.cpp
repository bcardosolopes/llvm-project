// A closure the merge gives a canonical name keeps its ABI facts.
//
// Fat-object CIR carries no AST, so lowering reads whether a record must be
// passed indirectly from the module's record ABI table, keyed by record name.
// The merge renames a later input's anonymous records to `anon.m<N>`; the
// table followed only the named records' renames, so a closure's entry stayed
// under its per-TU `anon.<n>`, lowering found none, and the closure -- which
// holds a G, non-trivial to copy -- was passed in a register. Caller and callee
// then each owned a G: a lambda holding a reference-counted handle, passed by
// value to std::function's constructor, released its reference twice.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -O1 -fclangir \
// RUN:     -fclangir-fat-objects -c %t/first.cpp -o %t/first.o
// RUN: %clang --target=aarch64-unknown-linux-android -O1 -fclangir \
// RUN:     -fclangir-fat-objects -c %t/second.cpp -o %t/second.o
// RUN: cir-link %t/first.o %t/second.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/out.ll
// RUN: FileCheck --input-file=%t/out.ll %s
//
// The closure goes by invisible reference, as clang's own codegen passes it;
// in registers it would be an i64.
// CHECK: define {{.*}} @"_Z4takeIZ6secondlE3$_0EiT_"(ptr

//--- first.cpp
// Only here to give the merge an anonymous record before second.cpp's.
template <class F> __attribute__((noinline)) int takeTrivial(F f) {
  return f();
}
int first(int *x) {
  return takeTrivial([x]() { return *x; });
}

//--- second.cpp
struct G {
  long v;
  explicit G(long w) : v(w) {}
  G(G &&o) noexcept : v(o.v) { o.v = 0; }
  G(const G &) = delete;
  ~G();
};
G make(long);
int consume(long);
template <class F> __attribute__((noinline)) int take(F f) { return f(); }
int second(long x) {
  return take([g = make(x)]() { return consume(g.v); });
}
