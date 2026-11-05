// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

typedef short short8 __attribute__((ext_vector_type(8)));
typedef unsigned int u4 __attribute__((ext_vector_type(4)));

// CIR-LABEL: define{{.*}} @f1(
// CIR: call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> {{%.*}}, i1 true)
// OGCG-LABEL: define{{.*}} @f1(
// OGCG: call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> {{%.*}}, i1 true)
short8 f1(short8 a) { return __builtin_elementwise_clzg(a); }

// CIR-LABEL: define{{.*}} @f2(
// CIR: call <8 x i16> @llvm.cttz.v8i16(<8 x i16> {{%.*}}, i1 true)
// OGCG-LABEL: define{{.*}} @f2(
// OGCG: call <8 x i16> @llvm.cttz.v8i16(<8 x i16> {{%.*}}, i1 true)
short8 f2(short8 a) { return __builtin_elementwise_ctzg(a); }

// CIR-LABEL: define{{.*}} @f3(
// CIR: call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> {{%.*}}, i1 true)
// CIR: select <8 x i1> {{%.*}}, <8 x i16> {{%.*}}, <8 x i16> {{%.*}}
// OGCG-LABEL: define{{.*}} @f3(
// OGCG: call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> {{%.*}}, i1 true)
// OGCG: select <8 x i1> {{%.*}}, <8 x i16> {{%.*}}, <8 x i16> {{%.*}}
short8 f3(short8 a, short8 b) { return __builtin_elementwise_clzg(a, b); }

// CIR-LABEL: define{{.*}} @f4(
// CIR: call <8 x i16> @llvm.cttz.v8i16(<8 x i16> {{%.*}}, i1 true)
// CIR: select <8 x i1> {{%.*}}, <8 x i16> {{%.*}}, <8 x i16> {{%.*}}
// OGCG-LABEL: define{{.*}} @f4(
// OGCG: call <8 x i16> @llvm.cttz.v8i16(<8 x i16> {{%.*}}, i1 true)
// OGCG: select <8 x i1> {{%.*}}, <8 x i16> {{%.*}}, <8 x i16> {{%.*}}
short8 f4(short8 a, short8 b) { return __builtin_elementwise_ctzg(a, b); }

// CIR-LABEL: define{{.*}} @f5(
// CIR: call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> {{%.*}}, i1 true)
// OGCG-LABEL: define{{.*}} @f5(
// OGCG: call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> {{%.*}}, i1 true)
u4 f5(u4 a) { return __builtin_elementwise_clzg(a); }
