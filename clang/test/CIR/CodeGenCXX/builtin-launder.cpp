// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm -fstrict-vtable-pointers \
// RUN:   -std=c++11 -fclangir -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm -fstrict-vtable-pointers \
// RUN:   -std=c++11 -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that __builtin_launder emits llvm.launder.invariant.group under
// -fstrict-vtable-pointers for dynamic class types, matching OGCG.

struct TestVirtualFn {
  virtual void foo() {}
};

// LLVM-LABEL: @test_builtin_launder_virtual_fn
// LLVM: call ptr @llvm.launder.invariant.group
// OGCG-LABEL: @test_builtin_launder_virtual_fn
// OGCG: call ptr @llvm.launder.invariant.group
extern "C" void test_builtin_launder_virtual_fn(TestVirtualFn *p) {
  TestVirtualFn *d = __builtin_launder(p);
}

struct TestVirtualMember {
  TestVirtualFn member;
};

// LLVM-LABEL: @test_builtin_launder_virtual_member
// LLVM: call ptr @llvm.launder.invariant.group
// OGCG-LABEL: @test_builtin_launder_virtual_member
// OGCG: call ptr @llvm.launder.invariant.group
extern "C" void test_builtin_launder_virtual_member(TestVirtualMember *p) {
  TestVirtualMember *d = __builtin_launder(p);
}

// Negative case: non-dynamic types should NOT get launder.
struct TestNoInvariant {
  int x;
};

// LLVM-LABEL: @test_builtin_launder_ommitted
// LLVM-NOT: llvm.launder.invariant.group
// LLVM: ret void
// OGCG-LABEL: @test_builtin_launder_ommitted
// OGCG-NOT: llvm.launder.invariant.group
// OGCG: ret void
extern "C" void test_builtin_launder_ommitted(TestNoInvariant *p) {
  TestNoInvariant *d = __builtin_launder(p);
}

// Negative case: plain pointer types should NOT get launder.
// LLVM-LABEL: @test_builtin_launder_int
// LLVM-NOT: llvm.launder.invariant.group
// LLVM: ret void
// OGCG-LABEL: @test_builtin_launder_int
// OGCG-NOT: llvm.launder.invariant.group
// OGCG: ret void
extern "C" void test_builtin_launder_int(int *p) {
  int *d = __builtin_launder(p);
}
