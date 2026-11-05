// RUN: %clang_cc1 -std=c++23 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Test that C++23 static operator() is called without the implicit object
// argument. CXXOperatorCallExpr includes the object as the first argument,
// but for a static operator it must be emitted as an ignored expression and
// skipped when building call arguments.

struct Functor {
    static int operator()(int x, int y) {
        return x + y;
    }
};

void test() {
    Functor f;
    f(1, 2);
}

// CHECK: cir.func {{.*}} @_ZN7FunctorclEii(%arg0: !s32i {{.*}}, %arg1: !s32i
// CHECK:   %[[ADD:.*]] = cir.binop(add,
// CHECK:   cir.return %{{.*}} : !s32i

// CHECK: cir.func {{.*}} @_Z4testv
// CHECK:   %[[ONE:.*]] = cir.const #cir.int<1> : !s32i
// CHECK:   %[[TWO:.*]] = cir.const #cir.int<2> : !s32i
// CHECK:   cir.call @_ZN7FunctorclEii(%[[ONE]], %[[TWO]]) : (!s32i, !s32i) -> !s32i
