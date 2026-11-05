// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-cir -fsanitize=alignment,null,object-size -o %t.cir %s
// RUN: FileCheck %s --check-prefix=CIR < %t.cir
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm -fsanitize=alignment,null,object-size -o %t.ll %s
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu \
// RUN:   -emit-llvm -fsanitize=alignment,null,object-size -o %t.ogcg.ll %s
// RUN: FileCheck %s --check-prefix=OGCG < %t.ogcg.ll

struct CustomCopy {
  CustomCopy();
  CustomCopy(const CustomCopy &);
};

struct S1 {
  CustomCopy cc;
  bool b;
};

void assign_s1(S1 &dst, const S1 &src) { dst = src; }

static bool some_global_bool;

struct ExprCopy {
  ExprCopy();
  ExprCopy(const ExprCopy &, bool b = some_global_bool);
};

struct S2 {
  ExprCopy ec;
  bool b;
};

void assign_s2(S2 &dst, const S2 &src) { dst = src; }

struct CustomAssign {
  CustomAssign &operator=(const CustomAssign &);
};

struct S3 {
  CustomAssign ca;
  bool b;
};

S3 &assign_s3(S3 &dst, const S3 &src) { return dst = src; }

struct CustomMove {
  CustomMove();
  CustomMove(const CustomMove &&);
  CustomMove &operator=(const CustomMove &&);
};

struct S4 {
  CustomMove cm;
  bool b;
};

S4 &assign_s4(S4 &dst, S4 &src) { return dst = static_cast<S4 &&>(src); }

struct EnumCustomCopy {
  EnumCustomCopy();
  EnumCustomCopy(const EnumCustomCopy &);
};

struct S5 {
  EnumCustomCopy ecc;
  bool b;
};

void assign_s5(S5 &dst, const S5 &src) { dst = src; }

// CIR-NOT: @_ZN2S1aSERKS_
// CIR-NOT: @_ZN2S2aSERKS_
// CIR-NOT: @_ZN2S5aSERKS_

// CIR-LABEL: cir.func{{.*}} @_Z9assign_s1R2S1RKS_(
// CIR: cir.copy %{{.*}} to %{{.*}} {dst_alignment = 1 : i64, src_alignment = 1 : i64} : !cir.ptr<!rec_S1>

// CIR-LABEL: cir.func{{.*}} @_Z9assign_s2R2S2RKS_(
// CIR: cir.copy %{{.*}} to %{{.*}} {dst_alignment = 1 : i64, src_alignment = 1 : i64} : !cir.ptr<!rec_S2>

// CIR-LABEL: cir.func{{.*}} @_ZN2S3aSERKS_(
// CIR: cir.call @__cir_ubsan_type_mismatch_handler(%{{.*}}) {cir.nosanitize, {{.*}} cir.ubsan.type_check_kind = 1 : i8, {{.*}} cir.ubsan.type_name = "'bool'"} : (!u64i) -> ()
// CIR: cir.store align(1) %{{.*}}, %{{.*}} : !cir.bool, !cir.ptr<!cir.bool>

// CIR-LABEL: cir.func{{.*}} @_ZN2S4aSEOS_(
// CIR: cir.call @__cir_ubsan_type_mismatch_handler(%{{.*}}) {cir.nosanitize, {{.*}} cir.ubsan.type_check_kind = 1 : i8, {{.*}} cir.ubsan.type_name = "'bool'"} : (!u64i) -> ()
// CIR: cir.store align(1) %{{.*}}, %{{.*}} : !cir.bool, !cir.ptr<!cir.bool>

// CIR-LABEL: cir.func{{.*}} @_Z9assign_s5R2S5RKS_(
// CIR: cir.copy %{{.*}} to %{{.*}} {dst_alignment = 1 : i64, src_alignment = 1 : i64} : !cir.ptr<!rec_S5>

// LLVM-NOT: @_ZN2S1aSERKS_
// LLVM-NOT: @_ZN2S2aSERKS_
// LLVM-NOT: @_ZN2S5aSERKS_

// LLVM-LABEL: define{{.*}} void @_Z9assign_s1R2S1RKS_
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 1 %{{.*}}, i64 2, i1 false)

// LLVM-LABEL: define{{.*}} void @_Z9assign_s2R2S2RKS_
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 1 %{{.*}}, i64 2, i1 false)

// LLVM-LABEL: define{{.*}} void @_Z9assign_s5R2S5RKS_
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 1 %{{.*}}, i64 2, i1 false)

// OGCG-NOT: @_ZN2S1aSERKS_
// OGCG-NOT: @_ZN2S2aSERKS_
// OGCG-NOT: @_ZN2S5aSERKS_

// OGCG-LABEL: define{{.*}} void @_Z9assign_s1R2S1RKS_
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 1 %{{.*}}, i64 2, i1 false)

// OGCG-LABEL: define{{.*}} void @_Z9assign_s2R2S2RKS_
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 1 %{{.*}}, i64 2, i1 false)

// OGCG-LABEL: define{{.*}} void @_Z9assign_s5R2S5RKS_
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 1 %{{.*}}, i64 2, i1 false)
