// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that brace-initialized auto& references (InitListExpr as lvalue)
// are correctly lowered through CIR.

struct S { int x; };
S getS() { return {42}; }

void test() {
    const auto& s{getS()};
    (void)s.x;
}

// CHECK-LABEL: @_Z4testv
// CHECK: %[[TMP:.*]] = alloca %struct.S
// CHECK: %[[REF:.*]] = alloca ptr
// CHECK: call i32 @_Z4getSv()
// CHECK: store ptr %[[TMP]], ptr %[[REF]]
// CHECK: %[[LOAD:.*]] = load ptr, ptr %[[REF]]
// CHECK: getelementptr inbounds nuw %struct.S, ptr %[[LOAD]], i32 0, i32 0
