// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s

// CIR uses i32 GEP index instead of i64 for base class pointer
// adjustments on 64-bit targets.

struct A { int a; };
struct B { int b; };
struct X : A, B { int x; };

// In the derived-to-base cast for B, the pointer needs to be adjusted
// by the offset of B within X. On x86_64, the GEP index should use i64.
// CHECK-LABEL: define {{.*}} @_Z8get_baseP1X
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 4
B *get_base(X *p) {
  return p;
}
