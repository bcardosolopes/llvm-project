// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t-cir.ll
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ll

// Regression test: volatile aggregate return values should produce volatile
// stores, matching OGCG behavior. Previously the volatile flag was lost
// because ReturnValueSlot did not carry it and emitAggregateStore always
// used isVolatile=false.

struct S { int a, b; };
S make_s();

// CIR-LABEL: @_Z17test_volatile_aggv
// LLVM-LABEL: @_Z17test_volatile_aggv
// OGCG-LABEL: @_Z17test_volatile_aggv
void test_volatile_agg() {
  volatile S s = make_s();
  // CIR: cir.call @_Z6make_sv
  // CIR: cir.store volatile
  // LLVM: call i64 @_Z6make_sv()
  // LLVM: store volatile i64
  // OGCG: call i64 @_Z6make_sv()
  // OGCG: store volatile i64
}

// CIR-LABEL: @_Z19test_normal_agg_retv
// LLVM-LABEL: @_Z19test_normal_agg_retv
// OGCG-LABEL: @_Z19test_normal_agg_retv
void test_normal_agg_ret() {
  S s = make_s();
  // CIR: cir.call @_Z6make_sv
  // CIR-NOT: cir.store volatile
  // CIR: cir.store
  // LLVM: call i64 @_Z6make_sv()
  // LLVM-NOT: store volatile
  // LLVM: store i64
  // OGCG: call i64 @_Z6make_sv()
  // OGCG-NOT: store volatile
  // OGCG: store i64
}

// CIR-LABEL: @_Z22test_volatile_int_callv
// LLVM-LABEL: @_Z22test_volatile_int_callv
// OGCG-LABEL: @_Z22test_volatile_int_callv
int get_int();
void test_volatile_int_call() {
  volatile int x = get_int();
  // CIR: cir.call @_Z7get_intv
  // CIR: cir.store volatile
  // LLVM: call {{.*}} @_Z7get_intv()
  // LLVM: store volatile i32
  // OGCG: call {{.*}} @_Z7get_intv()
  // OGCG: store volatile i32
}
