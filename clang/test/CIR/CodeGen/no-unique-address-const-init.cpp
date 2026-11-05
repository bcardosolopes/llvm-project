// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify ConstantAggregateBuilder::split correctly decomposes aggregate
// constants for [[no_unique_address]] member initializers. When a struct
// member reuses tail padding of a potentially-overlapping subobject, the
// builder must split the aggregate constant to place the next member inside
// the tail padding.

struct A { ~A(); int n; char c[3]; };

// B has [[no_unique_address]] A: A's data size is 7 (i32 + [3 x i8]) but
// its full size is 8 (padded to alignment). The tail padding byte is reused
// by k. The constant initializer must split the A aggregate.
struct B { [[no_unique_address]] A a; char k; };
// CHECK-DAG: @b = {{.*}}global { i32, [3 x i8], i8 } { i32 1, [3 x i8] c"\02\03\04", i8 5 }
// OGCG-DAG: @b = {{.*}}global { i32, [3 x i8], i8 } { i32 1, [3 x i8] c"\02\03\04", i8 5 }
B b = {1, 2, 3, 4, 5};
