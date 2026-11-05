// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -emit-llvm -fclangir -o - %s | FileCheck %s

struct A { int x, y[3]; };
struct B { A a; };
B b = {(A){1, 2, 3, 4}, .a.y[1] = 5};

union U {
  int n;
  float f;
};
struct C {
  int x;
  U u[3];
};
struct D {
  C c;
};

struct Bitfield {
  int a : 3;
  int b : 4;
  int c : 5;
};
struct WithBitfield {
  int n;
  Bitfield b;
};

union OverwritePaddingWithBitfield {
  struct Padding { unsigned : 8; char c; } padding;
  char bitfield : 3;
};
struct WithOverwritePaddingWithBitfield {
  OverwritePaddingWithBitfield a;
};

D d1 = {(C){1, {{.n=2}, {.f=3}, {.n=4}}}, .c.u[1].n = 5};
D d2 = {(C){1, 2, 3, 4}, .c.u[1].f = 5};
WithBitfield bitfield = {1, (Bitfield){2, 3, 4}, .b.b = 5};
WithOverwritePaddingWithBitfield overwrite_padding = {
    (OverwritePaddingWithBitfield){1}, .a.bitfield = 3};

// CHECK: @b ={{.*}} global %struct.B { %struct.A { i32 1, [3 x i32] [i32 2, i32 5, i32 4] } }
// CHECK: @d1 ={{.*}} global {{.*}} i32 1{{.*}}i32 2{{.*}}i32 5{{.*}}i32 4
// CHECK: @d2 ={{.*}} global {{.*}} i32 1{{.*}}i32 2{{.*}}float 5.000000e+00{{.*}}i32 4
// CHECK: @bitfield ={{.*}} global { i32, { i8, i8, [2 x i8] } } { i32 1, { i8, i8, [2 x i8] } { i8 42, i8 2, [2 x i8] undef } }
// CHECK: @overwrite_padding ={{.*}} global { { i8, i8 } } { { i8, i8 } { i8 3, i8 1 } }
// CHECK-NOT: @__cxx_global_var_init
