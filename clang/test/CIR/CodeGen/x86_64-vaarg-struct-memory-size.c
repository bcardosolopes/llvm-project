// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression: x86_64 va_arg for memory-only structs whose size is not a
// multiple of 8 must copy only the type's alloc size (not the ABI slot
// stride). Copying the stride (e.g. 32 for a 31-byte struct) would overflow
// the destination buffer.

typedef struct { char x[31]; } S31;
typedef struct { char x[35]; } S35;

// CIR-LABEL: define {{.*}} @get_s31(
// CIR: call void @llvm.memcpy.p0.p0.i64({{.*}}, i64 31, i1 false)

// OGCG-LABEL: define {{.*}} @get_s31(
// OGCG: call void @llvm.memcpy.p0.p0.i64({{.*}}, i64 31, i1 false)
S31 get_s31(int d, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, d);
  S31 r = __builtin_va_arg(ap, S31);
  __builtin_va_end(ap);
  return r;
}

// CIR-LABEL: define {{.*}} @get_s35(
// CIR: call void @llvm.memcpy.p0.p0.i64({{.*}}, i64 35, i1 false)

// OGCG-LABEL: define {{.*}} @get_s35(
// OGCG: call void @llvm.memcpy.p0.p0.i64({{.*}}, i64 35, i1 false)
S35 get_s35(int d, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, d);
  S35 r = __builtin_va_arg(ap, S35);
  __builtin_va_end(ap);
  return r;
}
