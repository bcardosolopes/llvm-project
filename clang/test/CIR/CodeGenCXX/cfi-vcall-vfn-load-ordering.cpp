// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-vcall -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden \
// RUN:   -fsanitize=cfi-vcall -emit-llvm -o - %s | FileCheck %s

// Verify that the vfn GEP + load are placed AFTER the CFI check branch
// (in the cont block), not before it. This matches OGCG which loads
// the virtual function pointer only on the success path.

struct S { virtual void f(); };
void test(S *s) { s->f(); }

// The entry block should end with the branch after the type.test calls.
// CHECK:      call i1 @llvm.type.test(
// CHECK:      call i1 @llvm.type.test(
// CHECK-NEXT: br i1

// The GEP + load should be in the cont block, after the branch.
// CHECK:      cont:
// CHECK-NEXT: getelementptr inbounds ptr
// CHECK-NEXT: load ptr
