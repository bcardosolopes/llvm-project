// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s

// Test that ARM method pointer ABI is correctly implemented for AArch64.
// AArch64 uses the ARM variant where:
// - virtual flag is in adj & 1 (not ptr & 1)
// - this-adjustment is adj >> 1 (arithmetic shift right)
// - virtual offset is ptr directly (no ptr - 1)

struct Base {
  virtual void foo();
  virtual void bar();
  void nonvirt();
};

void (Base::*pv)() = &Base::foo;
void (Base::*pn)() = &Base::nonvirt;

// ARM ABI constant encoding for member function pointers:
// - virtual: ptr = vtable offset, adj = 2*this_adj + 1
// - non-virtual: ptr = function address, adj = 2*this_adj
// CHECK: @pv ={{.*}}global { i64, i64 } { i64 0, i64 1 }
// CHECK: @pn ={{.*}}global { i64, i64 } { i64 ptrtoint (ptr @_ZN4Base7nonvirtEv to i64), i64 0 }

// CHECK-LABEL: @_Z11call_methodR4BaseMS_FvvE
// CHECK: %[[ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// ARM ABI: shift adj right by 1 for the real this-adjustment
// CHECK: %[[SHIFTED:.*]] = ashr i64 %[[ADJ]], 1
// ARM ABI: virtual check uses adj & 1, not ptr & 1
// CHECK: %[[VBIT:.*]] = and i64 %[[ADJ]], 1
void call_method(Base &b, void (Base::*method)()) {
  (b.*method)();
}
