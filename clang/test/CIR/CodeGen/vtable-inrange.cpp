// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c++17 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Verify that VTableAddrPointOp carries vtable_size and address_point_offset
// attributes for inrange GEP computation.

struct A { virtual ~A(); };
struct B : A { virtual ~B(); };

void test() { B b; }

// CHECK: cir.vtable.address_point({{.*}}, address_point = <index = 0, offset = 2>)
// CHECK-SAME: address_point_offset = 16
// CHECK-SAME: vtable_size = 32
