// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=OGCG

enum Numbers {
  Zero,
  One,
  Two,
  Three
};

int f() {
  return Numbers::One;
}

// CIR: cir.func{{.*}} @_Z1fv
// CIR:    cir.const #cir.int<1> : !s32i

// OGCG: define {{.*}} i32 @_Z1fv()
// OGCG:   ret i32 1

namespace test {
  using enum Numbers;
};

int f2() {
  return test::Two;
}

// CIR: cir.func{{.*}} @_Z2f2v
// CIR:    cir.const #cir.int<2> : !s32i

// OGCG: define {{.*}} i32 @_Z2f2v()
// OGCG:   ret i32 2

// Test enum with bool underlying type. The implicit conversion from
// enum : bool to bool should not generate an invalid int_to_bool cast.
enum BoolState : bool { Off = false, On = true };

bool f3(BoolState s) {
  return s;
}

// CIR: cir.func{{.*}} @_Z2f3{{.*}}
// CIR-NOT: cir.cast(int_to_bool

// OGCG: define {{.*}} i1 @_Z2f39BoolState(i1 {{.*}} %s)
// OGCG:   %[[SADDR:.*]] = alloca i8
// OGCG:   %[[LOADED:.*]] = load i8, ptr %[[SADDR]]
// OGCG:   %[[TRUNC:.*]] = trunc i8 %[[LOADED]] to i1
// OGCG:   %[[CMP:.*]] = icmp ne i1 %[[TRUNC]], false
// OGCG:   ret i1 %[[CMP]]
