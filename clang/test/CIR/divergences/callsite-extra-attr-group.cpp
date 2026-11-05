// RUN: %clang -S -emit-llvm -O0 -fno-clangir %s -o - | FileCheck %s --check-prefix=CHECK
// RUN: %clang -S -emit-llvm -O0 -fclangir %s -o - | FileCheck %s --check-prefix=CHECK
//
// Previously
//
// The divergence (ClangIR materializing an extra call-site attr group on
// direct calls where OGCG emits a plain call) is now fixed: calls to
// functions without explicit noexcept / __attribute__((nothrow)) no longer
// receive nothrow in CIR, so no nounwind attr group is emitted.

void foo() {}
void test() { foo(); }

// CHECK-LABEL: define {{.*}} @_Z4testv()
// CHECK:         call void @_Z3foov()
// CHECK-NEXT:    ret void
