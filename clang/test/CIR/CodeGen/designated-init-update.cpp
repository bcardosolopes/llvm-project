// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that DesignatedInitUpdateExpr constant-folds correctly in CIR codegen.
// The expression partially updates an aggregate that was already initialized
// by a compound literal.

struct A { int x, y[3]; };
struct B { A a; };

// Nested struct + array: constant folded.
// CHECK: @b ={{.*}} global %struct.B { %struct.A { i32 1, [3 x i32] [i32 2, i32 5, i32 4] } }
B b = {(A){1, 2, 3, 4}, .a.y[1] = 5};

// String buffer patching: constant folded.
struct String { const char buffer[12]; };
struct WithString { String str; };
// CHECK: @string ={{.*}} global %struct.WithString { %struct.String { [12 x i8] c"Hello World\00" } }
WithString string = {(String){"hello world"}, .str.buffer[0] = 'H', .str.buffer[6] = 'W'};

// Union array with homogeneous update: constant folded.
union U { int n; float f; };
struct C { int x; U u[3]; };
struct D { C c; };
// CHECK-NOT: @d1 ={{.*}} zeroinitializer
// CHECK: @d1 ={{.*}} global
// CHECK-SAME: i32 1
// CHECK-SAME: i32 2
// CHECK-SAME: i32 5
// CHECK-SAME: i32 4
D d1 = {(C){1, {{.n=2}, {.f=3}, {.n=4}}}, .c.u[1].n = 5};

// Union array with heterogeneous update (int→float): constant folded.
// CHECK-NOT: @d2 ={{.*}} zeroinitializer
// CHECK: @d2 ={{.*}} global
// CHECK-SAME: i32 1
// CHECK-SAME: i32 2
// CHECK-SAME: float 5.000000e+00
// CHECK-SAME: i32 4
D d2 = {(C){1, 2, 3, 4}, .c.u[1].f = 5};

// Bitfield update: constant folded via ConstRecordBuilder byte-level update.
struct Bitfield { int a : 3; int b : 4; int c : 5; };
struct WithBitfield { int n; Bitfield b; };
// CHECK: @bitfield = global { i32, { i8, i8, [2 x i8] } } { i32 1, { i8, i8, [2 x i8] } { i8 42, i8 2, [2 x i8] undef } }
WithBitfield bitfield = {1, (Bitfield){2, 3, 4}, .b.b = 5};
