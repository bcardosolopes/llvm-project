// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that __attribute__((flatten)) adds alwaysinline to call sites,
// except for noinline callees.

void f(void) {}

__attribute__((noinline)) void ni(void) {}

__attribute__((flatten))
void g(void) {
  f();
  ni();
}

void h(void) {
  f();
}

// The alwaysinline extra attribute alias is defined at the top.
// CIR: #[[AI:.*]] = #cir<extra({alwaysinline})>

// In CIR, the call to f() inside g() (flatten) gets alwaysinline,
// but the call to ni() (noinline) and calls in h() do not.
// CIR: cir.func {{.*}} @g()
// CIR: cir.call @f() {{.*}}extra(#[[AI]])
// CIR: cir.call @ni()
// CIR-NOT: extra(#[[AI]])
// CIR: cir.func {{.*}} @h()
// CIR: cir.call @f()
// CIR-NOT: extra(#[[AI]])

// f() should be inlined in g() (flatten) but not in h().
// LLVM: define dso_local void @g()
// LLVM-NOT: call {{.*}} @f
// LLVM: call void @ni()
// LLVM: define dso_local void @h()
// LLVM: call void @f()

// OGCG: define dso_local void @g()
// OGCG-NOT: call {{.*}} @f
// OGCG: call void @ni()
// OGCG: define dso_local void @h()
// OGCG: call void @f()
