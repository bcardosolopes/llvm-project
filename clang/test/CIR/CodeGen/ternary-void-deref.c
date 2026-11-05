// Verify that CIR handles ternary expressions with void pointer dereference
// without crashing. Dereferencing a void* is a GCC extension that produces
// a void-typed expression; the ternary result is also void.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

extern void *f_v(void);
extern const void *f_cv(void);
extern volatile void *f_vv(void);

void test(void) {
  // Simple dereference of void* — no crash.
  *f_v();
  *f_cv();

  // Comma operator with void* deref — no crash.
  *f_v(), *f_v();

  // Ternary with void* deref in both arms — this was the crash case.
  f_v() ? *f_v() : *f_v();
  f_cv() ? *f_cv() : *f_cv();
  f_vv() ? *f_vv() : *f_vv();
}

// CHECK-LABEL: define {{.*}} @test()
// CHECK: call ptr @f_v()
// CHECK: call ptr @f_v()
// CHECK: call ptr @f_v()
// CHECK: call ptr @f_v()
// CHECK: ret void

// OGCG-LABEL: define {{.*}} @test()
// OGCG: call ptr @f_v()
// OGCG: call ptr @f_v()
// OGCG: call ptr @f_v()
// OGCG: call ptr @f_v()
// OGCG: ret void
