// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -fclangir-mem2reg -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression: indirect AAPCS64 va_arg records with tail padding must copy the
// full ABI object size from the referenced slot, not perform a typed load/store
// that drops trailing padding bytes.

struct s17 {
  long long a;
  long long b;
  char c;
};

// CIR-LABEL: define{{.*}} @test_tail_padding(
// CIR: %[[DST:.*]] = alloca %struct.s17, i64 1, align 8
// CIR: load ptr, ptr %{{.*}}, align 8
// CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %[[DST]], ptr align 8 %{{.*}}, i64 24, i1 false)
// CIR: %{{.*}} = getelementptr inbounds nuw %struct.s17, ptr %[[DST]], i32 0, i32 2

// OGCG-LABEL: define{{.*}} @test_tail_padding(
// OGCG: load ptr, ptr %{{.*}}, align 8
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 24, i1 false)
char test_tail_padding(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct s17 s = __builtin_va_arg(ap, struct s17);
  __builtin_va_end(ap);
  return s.c;
}
