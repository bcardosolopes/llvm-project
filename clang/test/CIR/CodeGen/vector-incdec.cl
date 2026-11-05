// RUN: %clang_cc1 -cl-std=CL3.0 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -cl-std=CL3.0 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
// RUN: %clang_cc1 -cl-std=CL3.0 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef int int4 __attribute__((ext_vector_type(4)));

// CIR-LABEL: @test_int_vec_pre_inc
// OGCG-LABEL: @test_int_vec_pre_inc
void test_int_vec_pre_inc(int4 *p) {
  ++(*p);
}
// CIR: %[[VAL:.*]] = cir.load {{.*}} : !cir.ptr<!cir.vector<4 x !s32i>{{.*}}>, !cir.vector<4 x !s32i>
// CIR: %[[CST:.*]] = cir.const #cir.const_vector<[#cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i, #cir.int<1> : !s32i]> : !cir.vector<4 x !s32i>
// CIR: %[[RES:.*]] = cir.binop(add, %[[VAL]], %[[CST]]) : !cir.vector<4 x !s32i>
// CIR: cir.store {{.*}}%[[RES]], {{.*}} : !cir.vector<4 x !s32i>

// OGCG: %[[LD:.*]] = load <4 x i32>, ptr %{{.*}}
// OGCG: %[[RES:.*]] = add <4 x i32> %[[LD]], splat (i32 1)
// OGCG: store <4 x i32> %[[RES]], ptr %{{.*}}

// CIR-LABEL: @test_int_vec_pre_dec
// OGCG-LABEL: @test_int_vec_pre_dec
void test_int_vec_pre_dec(int4 *p) {
  --(*p);
}
// CIR: %[[VAL:.*]] = cir.load {{.*}} : !cir.ptr<!cir.vector<4 x !s32i>{{.*}}>, !cir.vector<4 x !s32i>
// CIR: %[[CST:.*]] = cir.const #cir.const_vector<[#cir.int<-1> : !s32i, #cir.int<-1> : !s32i, #cir.int<-1> : !s32i, #cir.int<-1> : !s32i]> : !cir.vector<4 x !s32i>
// CIR: %[[RES:.*]] = cir.binop(add, %[[VAL]], %[[CST]]) : !cir.vector<4 x !s32i>
// CIR: cir.store {{.*}}%[[RES]], {{.*}} : !cir.vector<4 x !s32i>

// OGCG: %[[LD:.*]] = load <4 x i32>, ptr %{{.*}}
// OGCG: %[[RES:.*]] = add <4 x i32> %[[LD]], splat (i32 -1)
// OGCG: store <4 x i32> %[[RES]], ptr %{{.*}}
