// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu -std=c++20 %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG-MATCH %s < %t.ll

// Sret alignment wrong for classes with inheritance
// Non-virtual inheritance with vtable pointer should produce align 8
// Virtual inheritance should also produce align 8
//
// (stale FIXME cleanup + explicit parity checks).

struct Base { int x; virtual void f() {} };

struct NonVirt : Base { int y; };
NonVirt make_nv() { NonVirt n; return n; }

// OGCG-MATCH: define {{.*}} @_Z7make_nvv(ptr {{.*}} sret(%struct.NonVirt) align 8

struct Virt : virtual Base { int y; };
Virt make_v() { Virt v; return v; }

// OGCG-MATCH: define {{.*}} @_Z6make_vv(ptr {{.*}} sret(%struct.Virt) align 8
