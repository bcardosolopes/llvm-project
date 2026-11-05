// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression: x86_64 va_arg memory-only aggregates with tail padding must use
// a byte copy of the ABI slot size (64 bytes here), matching OGCG's strategy.

typedef int T44 __attribute((vector_size(32)));
struct s44 {
  T44 x;
  int y;
};

// CIR-LABEL: define {{.*}} @f44(
// CIR: %[[DST:.*]] = alloca %struct.s44, i64 1, align 32
// CIR: %[[VAADDR:.*]] = {{(tail )?}}call{{( align 32)?}} ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -32)
// CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 32 %[[DST]], ptr align 32 %[[VAADDR]], i64 64, i1 false)
// CIR: %[[Y:.*]] = getelementptr inbounds nuw %struct.s44, ptr %[[DST]], i32 0, i32 1

// OGCG-LABEL: define {{.*}} @f44(
// OGCG: %[[ALIGNED:.*]] = {{(tail )?}}call{{( align 32)?}} ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -32)
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 32 %{{.*}}, ptr align 32 %[[ALIGNED]], i64 64, i1 false)
int f44(int i, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, i);
  struct s44 s = __builtin_va_arg(ap, struct s44);
  __builtin_va_end(ap);
  return s.y;
}
