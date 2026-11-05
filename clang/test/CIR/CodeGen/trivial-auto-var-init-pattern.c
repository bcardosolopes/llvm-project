// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -ftrivial-auto-var-init=pattern %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -ftrivial-auto-var-init=pattern %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -ftrivial-auto-var-init=pattern %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test -ftrivial-auto-var-init=pattern fills variables with 0xAA pattern.
// On x86_64 (64-bit pointers), the pattern byte is 0xAA.

// Check global constants are emitted with pattern values.
// LLVM-DAG: @__const.test_array.arr = {{.*}}constant [4 x i32] [i32 -1431655766, i32 -1431655766, i32 -1431655766, i32 -1431655766]
// LLVM-DAG: @__const.test_struct.s = {{.*}}constant %struct.S { i32 -1431655766, float 0xFFFFFFFFE0000000 }

void test_int() {
  int x;
}

// CIR-LABEL: @test_int
// CIR: %[[X:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init]
// CIR: %[[C:.*]] = cir.const #cir.int<-1431655766> : !s32i
// CIR: cir.store align(4) %[[C]], %[[X]]

// OGCG-LABEL: @test_int
// OGCG: store i32 -1431655766
// LLVM-LABEL: @test_int
// LLVM: store i32 -1431655766, ptr %{{.*}}, align 4, !annotation [[AUTO_INIT:![0-9]+]]

void test_array() {
  int arr[4];
}

// CIR-LABEL: @test_array
// CIR: cir.get_global @__const.test_array.arr
// CIR: cir.copy

// OGCG-LABEL: @test_array
// OGCG: call void @llvm.memcpy
// LLVM-LABEL: @test_array
// LLVM: call void @llvm.memcpy{{.*}}, !annotation [[AUTO_INIT]]

void test_float() {
  float f;
}

// CIR-LABEL: @test_float
// CIR: %[[F:.*]] = cir.alloca !cir.float, !cir.ptr<!cir.float>, ["f", init]
// CIR: %[[C:.*]] = cir.const #cir.fp<0xFFFFFFFF> : !cir.float
// CIR: cir.store align(4) %[[C]], %[[F]]

// OGCG-LABEL: @test_float
// OGCG: store float 0xFFFFFFFFE0000000
// LLVM-LABEL: @test_float
// LLVM: store float 0xFFFFFFFFE0000000, ptr %{{.*}}, align 4

void test_double() {
  double d;
}

// CIR-LABEL: @test_double
// CIR: %[[D:.*]] = cir.alloca !cir.double, !cir.ptr<!cir.double>, ["d", init]
// CIR: %[[C:.*]] = cir.const #cir.fp<0xFFFFFFFFFFFFFFFF> : !cir.double
// CIR: cir.store align(8) %[[C]], %[[D]]

// OGCG-LABEL: @test_double
// OGCG: store double 0xFFFFFFFFFFFFFFFF
// LLVM-LABEL: @test_double
// LLVM: store double 0xFFFFFFFFFFFFFFFF, ptr %{{.*}}, align 8

struct S { int x; float y; };

void test_struct() {
  struct S s;
}

// CIR-LABEL: @test_struct
// CIR: cir.get_global @__const.test_struct.s
// CIR: cir.copy

// OGCG-LABEL: @test_struct
// OGCG: call void @llvm.memcpy

void test_ptr() {
  int *p;
}

// CIR-LABEL: @test_ptr
// CIR: %[[P:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["p", init]
// CIR: %[[C:.*]] = cir.const #cir.ptr<-6148914691236517206 : i64> : !cir.ptr<!s32i>
// CIR: cir.store align(8) %[[C]], %[[P]]

// OGCG-LABEL: @test_ptr
// OGCG: store ptr inttoptr (i64 -6148914691236517206 to ptr)
// LLVM-LABEL: @test_ptr
// LLVM: store ptr inttoptr (i64 -6148914691236517206 to ptr), ptr %{{.*}}, align 8, !annotation [[AUTO_INIT]]

// LLVM: [[AUTO_INIT]] = !{!"auto-init"}
