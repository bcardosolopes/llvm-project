// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Regression test: anonymous union member init must use the union's CIR record
// type for GetMemberOp, not the outer struct's type.

struct Outer {
  int tag;
  union {
    int ival;
    double dval;
    long long lval;
  };
  Outer() : tag(0) {}
  Outer(int v) : tag(1), ival(v) {}
  Outer(double v) : tag(2), dval(v) {}
  Outer(long long v) : tag(3), lval(v) {}
};

void test() {
  Outer a;
  Outer b(42);
  Outer c(3.14);
  Outer d(100LL);
}

// Verify constructor Outer(int): accesses union member "ival" through the
// anonymous union record type, not through the outer struct directly.
//
// CHECK: cir.func {{.*}}@_ZN5OuterC2Ei
// CHECK:   cir.get_member %{{[0-9]+}}[0] {name = "tag"} : !cir.ptr<!rec_Outer> -> !cir.ptr<!s32i>
// CHECK:   cir.get_member %{{[0-9]+}}[1] {name = ""} : !cir.ptr<!rec_Outer> -> !cir.ptr<!rec_anon
// CHECK:   cir.get_member %{{[0-9]+}}[0] {name = "ival"} : !cir.ptr<!rec_anon{{.*}}> -> !cir.ptr<!s32i>

// Verify constructor Outer(double): accesses "dval" at union index 1.
// CHECK: cir.func {{.*}}@_ZN5OuterC2Ed
// CHECK:   cir.get_member %{{[0-9]+}}[1] {name = ""} : !cir.ptr<!rec_Outer> -> !cir.ptr<!rec_anon
// CHECK:   cir.get_member %{{[0-9]+}}[1] {name = "dval"} : !cir.ptr<!rec_anon{{.*}}> -> !cir.ptr<!cir.double>

// Verify constructor Outer(long long): accesses "lval" at union index 2.
// CHECK: cir.func {{.*}}@_ZN5OuterC2Ex
// CHECK:   cir.get_member %{{[0-9]+}}[1] {name = ""} : !cir.ptr<!rec_Outer> -> !cir.ptr<!rec_anon
// CHECK:   cir.get_member %{{[0-9]+}}[2] {name = "lval"} : !cir.ptr<!rec_anon{{.*}}> -> !cir.ptr<!s64i>

// OGCG: define {{.*}} @_Z4testv()
// OGCG: define {{.*}} @_ZN5OuterC{{[12]}}Ev(ptr {{.*}} %this)
// OGCG:   %tag = getelementptr inbounds nuw %struct.Outer, ptr %{{.*}}, i32 0, i32 0
// OGCG:   store i32 0, ptr %tag
//
// OGCG: define {{.*}} @_ZN5OuterC{{[12]}}Ei(ptr {{.*}} %this, i32 {{.*}} %v)
// OGCG:   %tag = getelementptr inbounds nuw %struct.Outer, ptr %{{.*}}, i32 0, i32 0
// OGCG:   store i32 1, ptr %tag
// OGCG:   getelementptr inbounds nuw %struct.Outer, ptr %{{.*}}, i32 0, i32 1
// OGCG:   store i32 %{{.*}}, ptr %{{.*}}
//
// OGCG: define {{.*}} @_ZN5OuterC{{[12]}}Ed(ptr {{.*}} %this, double {{.*}} %v)
// OGCG:   %tag = getelementptr inbounds nuw %struct.Outer, ptr %{{.*}}, i32 0, i32 0
// OGCG:   store i32 2, ptr %tag
// OGCG:   getelementptr inbounds nuw %struct.Outer, ptr %{{.*}}, i32 0, i32 1
// OGCG:   store double %{{.*}}, ptr %{{.*}}
//
// OGCG: define {{.*}} @_ZN5OuterC{{[12]}}Ex(ptr {{.*}} %this, i64 {{.*}} %v)
// OGCG:   %tag = getelementptr inbounds nuw %struct.Outer, ptr %{{.*}}, i32 0, i32 0
// OGCG:   store i32 3, ptr %tag
// OGCG:   getelementptr inbounds nuw %struct.Outer, ptr %{{.*}}, i32 0, i32 1
// OGCG:   store i64 %{{.*}}, ptr %{{.*}}
