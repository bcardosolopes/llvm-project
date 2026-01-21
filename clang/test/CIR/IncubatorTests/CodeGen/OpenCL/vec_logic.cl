// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -O0 -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -O0 -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM

typedef char vc4 __attribute__((ext_vector_type(4)));

void test(vc4 in1, vc4 in2, vc4 *out) {
  *out = (in1 == (vc4)3 && (in1 == (vc4)5 || in2 == (vc4)7))
          ? in1 : in2;
}

// CIR: cir.const #cir.zero : !cir.vector<4 x !s8i>
// CIR: cir.vec.cmp(ne, %{{.*}}, %{{.*}}) : !cir.vector<4 x !s8i>, !cir.vector<4 x !cir.bool>
// CIR: cir.vec.cmp(ne, %{{.*}}, %{{.*}}) : !cir.vector<4 x !s8i>, !cir.vector<4 x !cir.bool>
// CIR: cir.binop(or, %{{.*}}, %{{.*}}) : !cir.vector<4 x !cir.bool>
// CIR: cir.cast bool_to_int %{{.*}} : !cir.vector<4 x !cir.bool> -> !cir.vector<4 x !s8i>
// CIR: cir.const #cir.zero : !cir.vector<4 x !s8i>
// CIR: cir.vec.cmp(ne, %{{.*}}, %{{.*}}) : !cir.vector<4 x !s8i>, !cir.vector<4 x !cir.bool>
// CIR: cir.vec.cmp(ne, %{{.*}}, %{{.*}}) : !cir.vector<4 x !s8i>, !cir.vector<4 x !cir.bool>
// CIR: cir.binop(and, %{{.*}}, %{{.*}}) : !cir.vector<4 x !cir.bool>
// CIR: cir.cast bool_to_int %{{.*}} : !cir.vector<4 x !cir.bool> -> !cir.vector<4 x !s8i>

// LLVM: icmp ne <4 x i8> %{{.*}}, zeroinitializer
// LLVM: icmp ne <4 x i8> %{{.*}}, zeroinitializer
// LLVM: or <4 x i1>
// LLVM: sext <4 x i1> %{{.*}} to <4 x i8>
// LLVM: icmp ne <4 x i8> %{{.*}}, zeroinitializer
// LLVM: icmp ne <4 x i8> %{{.*}}, zeroinitializer
// LLVM: and <4 x i1>
// LLVM: sext <4 x i1> %{{.*}} to <4 x i8>
