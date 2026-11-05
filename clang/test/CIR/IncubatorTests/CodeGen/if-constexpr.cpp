// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void if0() {
  int x = 0;
  if constexpr (0 == 0) {
    // Declare a variable with same name to be sure we handle the
    // scopes correctly
    int x = 2;
  } else {
    int x = 3;
  }
  if constexpr (0 == 1) {
    int x = 4;
  } else {
    int x = 5;
  }
  if constexpr (int x = 7; 8 == 8) {
    int y = x;
  } else {
    int y = 2*x;
  }
  if constexpr (int x = 9; 8 == 10) {
    int y = x;
  } else {
    int y = 3*x;
  }
  if constexpr (10 == 10) {
    int x = 20;
  }
  if constexpr (10 == 11) {
    int x = 30;
  }
  if constexpr (int x = 70; 80 == 80) {
    int y = 10*x;
  }
  if constexpr (int x = 90; 80 == 100) {
    int y = 11*x;
  }
}

// CHECK: cir.func {{.*}} @_Z3if0v() {{.*}}
// CHECK: cir.store{{.*}} %4, %0 : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK-NEXT: %5 = cir.const #cir.int<2> : !s32i loc({{.*}})
// CHECK-NEXT: cir.store{{.*}} %5, %1 : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK-NEXT: %6 = cir.const #cir.int<5> : !s32i loc({{.*}})
// CHECK-NEXT: cir.store{{.*}} %6, %2 : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK-NEXT: cir.scope {
// CHECK:   cir.store{{.*}} %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK:   cir.load{{.*}} %{{.*}} : !cir.ptr<!s32i>, !s32i loc({{.*}})
// CHECK:   cir.store{{.*}} %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK: } loc({{.*}})
// CHECK-NEXT: cir.scope {
// CHECK:   cir.store{{.*}} %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK:   cir.binop(mul,
// CHECK:   cir.store{{.*}} %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK: } loc({{.*}})
// CHECK: cir.store{{.*}} %7, %3 : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK-NEXT: cir.scope {
// CHECK:   cir.store{{.*}} %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK:   cir.binop(mul,
// CHECK:   cir.store{{.*}} %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK: } loc({{.*}})
// CHECK-NEXT: cir.scope {
// CHECK:   cir.store{{.*}} %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i> loc({{.*}})
// CHECK: } loc({{.*}})
// CHECK-NEXT: cir.return loc({{.*}})
