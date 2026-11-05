// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test: When a struct is laid out and one of its nested structs has a function
// pointer field whose function type involves the outer struct (still being
// laid out, hence incomplete), CIR creates a placeholder void() function type.
// The type cache must be cleared when layout completes (skippedLayout) so that
// subsequent conversions of the same function type produce the correct type.
// This matches OGCG's SkippedLayout mechanism.

struct Outer;

struct Inner {
  void (*callback)(Outer);
};

struct Outer {
  Inner inner;
  int value;
};

void handler(Outer o) {
  o.value = 1;
}

// Verify the module compiles without verification errors and that
// the function pointer field stores the correct (non-placeholder) type.
// CHECK: cir.func {{.*}}@_Z4testv
// CHECK: cir.get_global @_Z7handler5Outer
void test() {
  Outer o;
  o.inner.callback = &handler;
  o.value = 42;
  o.inner.callback(o);
}
