// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that pass_object_size implicit size parameter is correctly forwarded
// in delegating constructor calls (C1 -> C2).

struct A {
  A(void *const p __attribute__((pass_object_size(0))));
};
A::A(void *const p __attribute__((pass_object_size(0)))) {}

// Both C2 (base) and C1 (complete) constructors take (ptr, ptr, i64).
// C1 must forward all three arguments to C2.

// LLVM: define {{.*}}void @_ZN1AC2EPvU17pass_object_size0(ptr {{.*}}, ptr {{.*}}, i64 {{.*}})
// OGCG: define {{.*}}void @_ZN1AC2EPvU17pass_object_size0(ptr {{.*}}, ptr {{.*}}, i64 {{.*}})

// LLVM: define {{.*}}void @_ZN1AC1EPvU17pass_object_size0(ptr {{.*}}%[[THIS:.*]], ptr {{.*}}%[[P:.*]], i64 {{.*}}%[[SZ:.*]])
// LLVM:   call void @_ZN1AC2EPvU17pass_object_size0(ptr {{.*}}, ptr {{.*}}, i64 {{.*}})
// OGCG: define {{.*}}void @_ZN1AC1EPvU17pass_object_size0(ptr {{.*}}%[[THIS:.*]], ptr {{.*}}%[[P:.*]], i64 {{.*}}%[[SZ:.*]])
// OGCG:   call void @_ZN1AC2EPvU17pass_object_size0(ptr {{.*}}, ptr {{.*}}, i64 {{.*}})
