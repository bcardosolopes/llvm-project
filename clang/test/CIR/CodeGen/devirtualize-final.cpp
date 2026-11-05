// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

struct Base {
  virtual int f();
  virtual ~Base();
};

struct Final final : Base {
  int f() override;
};

// Calling a virtual method on an object of final type should be devirtualized
// to a direct call — no vtable dispatch.
int test_devirt_final() {
  Final f;
  return f.f();
}

// CHECK-LABEL: @_Z17test_devirt_finalv
// CHECK:         cir.call @_ZN5FinalC1Ev
// Direct call, not virtual dispatch:
// CHECK:         cir.call @_ZN5Final1fEv
// CHECK-NOT:     cir.vtable.get_virtual_fn_addr
// CHECK:         cir.return
