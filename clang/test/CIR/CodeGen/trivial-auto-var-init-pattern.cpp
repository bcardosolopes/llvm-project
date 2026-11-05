// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -ftrivial-auto-var-init=pattern -o %t-cir.cir
// RUN: FileCheck --input-file=%t-cir.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -ftrivial-auto-var-init=pattern %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm -ftrivial-auto-var-init=pattern %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test -ftrivial-auto-var-init=pattern with C++ types that need special
// handling: complex, virtual classes, member pointers, and explicit padding.

// Check global constants are emitted with pattern values.
//
// Complex: pattern-init both real and imaginary with NaN.
// LLVM-DAG: @__const._Z12test_complexv.cf = {{.*}}constant { float, float } { float 0xFFFFFFFFE0000000, float 0xFFFFFFFFE0000000 }
// OGCG-DAG: @__const._Z12test_complexv.cf = {{.*}}constant { float, float } { float 0xFFFFFFFFE0000000, float 0xFFFFFFFFE0000000 }
//
// Vtable struct: pattern-init uses 0xAA pattern for the vtable pointer.
// CIR-DAG: #cir.const_record<{#cir.pattern_init : !cir.vptr
// LLVM-DAG: @__const._Z14test_vtable_fnv.b = {{.*}}constant %struct.Base <{ ptr inttoptr (i64 -6148914691236517206 to ptr), i32 -1431655766
// OGCG-DAG: @__const._Z14test_vtable_fnv.b = {{.*}}constant %struct.Base <{ ptr inttoptr (i64 -6148914691236517206 to ptr), i32 -1431655766
//
// Data member pointer: CIR stores pattern in a global constant, OGCG inlines it.
// CIR-DAG: #cir.pattern_init : !cir.data_member
// LLVM-DAG: @__const._Z20test_data_member_ptrv.p = {{.*}}constant i64 -6148914691236517206
//
// Method pointer: pattern-init fills both i64 fields with 0xAA pattern.
// CIR-DAG: #cir.pattern_init : !cir.method
// LLVM-DAG: @__const._Z15test_method_ptrv.mp = {{.*}}constant { i64, i64 } { i64 -6148914691236517206, i64 -6148914691236517206 }
// OGCG-DAG: @__const._Z15test_method_ptrv.mp = {{.*}}constant { i64, i64 } { i64 -6148914691236517206, i64 -6148914691236517206 }
//
// Bool arrays should pattern the full storage byte.
// CIR-DAG: initial_value = #cir.const_array<[#cir.int<170> : !u8i, #cir.int<170> : !u8i, #cir.int<170> : !u8i, #cir.int<170> : !u8i]> : !cir.array<!u8i x 4>, {{.*}}sym_name = "__const._Z15test_bool_arrayv.arr"
// LLVM-DAG: @__const._Z15test_bool_arrayv.arr = {{.*}}constant [4 x i8] c"\AA\AA\AA\AA"
// OGCG-DAG: @__const._Z15test_bool_arrayv.arr = {{.*}}constant [4 x i8] c"\AA\AA\AA\AA"
//
// Explicit record padding should carry the pattern byte too.
// CIR-DAG: initial_value = #cir.const_record<{#cir.int<-86> : !s8i, #cir.const_array<[#cir.int<170> : !u8i, #cir.int<170> : !u8i, #cir.int<170> : !u8i]> : !cir.array<!u8i x 3>, #cir.int<-1431655766> : !s32i}> : !rec_anon_struct{{[0-9]*}}, {{.*}}sym_name = "__const._Z11test_paddedv.p"
// LLVM-DAG: @__const._Z11test_paddedv.p = {{.*}}constant { i8, [3 x i8], i32 } { i8 -86, [3 x i8] c"\AA\AA\AA", i32 -1431655766 }
// OGCG-DAG: @__const._Z11test_paddedv.p = {{.*}}constant { i8, [3 x i8], i32 } { i8 -86, [3 x i8] c"\AA\AA\AA", i32 -1431655766 }
// CIR-DAG: initial_value = #cir.const_record<{#cir.int<-21846> : !s16i, #cir.int<-86> : !s8i, #cir.const_array<[#cir.int<170> : !u8i]> : !cir.array<!u8i x 1>}> : !rec_anon_struct{{[0-9]*}}, {{.*}}sym_name = "__const._Z12test_tailpadv.t"
// LLVM-DAG: @__const._Z12test_tailpadv.t = {{.*}}constant { i16, i8, [1 x i8] } { i16 -21846, i8 -86, [1 x i8] c"\AA" }
// OGCG-DAG: @__const._Z12test_tailpadv.t = {{.*}}constant { i16, i8, [1 x i8] } { i16 -21846, i8 -86, [1 x i8] c"\AA" }

void test_complex() {
  _Complex float cf;
  (void)cf;
}

// LLVM-LABEL: @_Z12test_complexv
// LLVM: call void @llvm.memcpy
// OGCG-LABEL: @_Z12test_complexv
// OGCG: call void @llvm.memcpy

struct Base {
  virtual void foo();
  int x;
};

void test_vtable_fn() {
  Base b;
  (void)b;
}

// LLVM-LABEL: @_Z14test_vtable_fnv
// LLVM: call void @llvm.memcpy
// OGCG-LABEL: @_Z14test_vtable_fnv
// OGCG: call void @llvm.memcpy

struct Foo { int x; void bar(); };

void test_data_member_ptr() {
  int Foo::*p;
  (void)p;
}

// Both CIR and OGCG produce the same pattern value for data member pointers.
// CIR uses memcpy from a global, OGCG uses a direct store.
// LLVM-LABEL: @_Z20test_data_member_ptrv
// LLVM: call void @llvm.memcpy
// OGCG-LABEL: @_Z20test_data_member_ptrv
// OGCG: store i64 -6148914691236517206

void test_method_ptr() {
  void (Foo::*mp)();
  (void)mp;
}

// LLVM-LABEL: @_Z15test_method_ptrv
// LLVM: call void @llvm.memcpy
// OGCG-LABEL: @_Z15test_method_ptrv
// OGCG: call void @llvm.memcpy

struct padded {
  char c;
  int i;
};

struct tailpad {
  short s;
  char c;
};

void test_bool_array() {
  bool arr[4];
  (void)arr;
}

// LLVM-LABEL: @_Z15test_bool_arrayv
// LLVM: call void @llvm.memcpy
// OGCG-LABEL: @_Z15test_bool_arrayv
// OGCG: call void @llvm.memcpy

void test_padded() {
  padded p;
  (void)p;
}

// LLVM-LABEL: @_Z11test_paddedv
// LLVM: call void @llvm.memcpy
// OGCG-LABEL: @_Z11test_paddedv
// OGCG: call void @llvm.memcpy

void test_tailpad() {
  tailpad t;
  (void)t;
}

// LLVM-LABEL: @_Z12test_tailpadv
// LLVM: call void @llvm.memcpy
// OGCG-LABEL: @_Z12test_tailpadv
// OGCG: call void @llvm.memcpy
