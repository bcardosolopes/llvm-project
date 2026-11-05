// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
//
// Former divergence: direct-CIR used to generate unnecessary temporary
// allocas for return values.
//
// Keep the direct-return fast paths covered:
// - direct scalar calls like test() should not spill through __retval.
// - direct-CIR also returns the coerced aggregate-call result in test_struct()
//   directly instead of materializing a synthetic return slot at -O0.

int foo() {
    return 42;
}

int test() {
    return foo();
}

// CIR-LABEL: define dso_local noundef i32 @_Z4testv()
// CIR-NOT: alloca i32
// CIR: %[[CIR_CALL:.*]] = call noundef i32 @_Z3foov()
// CIR-NEXT: ret i32 %[[CIR_CALL]]

// OGCG-LABEL: define dso_local noundef i32 @_Z4testv()
// OGCG: entry:
// OGCG-NOT: alloca i32
// OGCG: %[[OG_CALL:.*]] = call noundef i32 @_Z3foov()
// OGCG-NEXT: ret i32 %[[OG_CALL]]

// Also affects struct returns
struct S {
    int x, y;
};

S bar() {
    return {1, 2};
}

S test_struct() {
    return bar();
}

// CIR-LABEL: define dso_local i64 @_Z11test_structv()
// CIR-NOT: alloca
// CIR: %[[CIR_STRUCT_CALL:.*]] = call i64 @_Z3barv()
// CIR-NEXT: ret i64 %[[CIR_STRUCT_CALL]]

// And void functions with calls
void baz() {}

void test_void() {
    baz();
}
