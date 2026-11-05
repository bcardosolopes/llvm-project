// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O -fclangir %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CHECK --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=CHECK --input-file=%t.ogcg.ll %s

typedef __builtin_va_list va_list;

long double test_ld(va_list ap) {
  return __builtin_va_arg(ap, long double);
}

// CHECK-LABEL: define {{.*}} x86_fp80 @test_ld(
// CHECK: %[[ROUND_UP:.*]] = getelementptr inbounds{{( nuw)?}} i8, ptr %{{.*}}, i64 15
// CHECK: %[[ALIGNED:.*]] = {{(tail )?}}call{{( align 16)?}} ptr @llvm.ptrmask.p0.i64(ptr {{(nonnull )?}}%[[ROUND_UP]], i64 -16)
// CHECK: %[[NEXT:.*]] = getelementptr{{( inbounds)?}}{{( nuw)?}} i8, ptr %[[ALIGNED]], i64 16
// CHECK: load x86_fp80, ptr %[[ALIGNED]], align 16

_Complex long double test_ldc(va_list ap) {
  return __builtin_va_arg(ap, _Complex long double);
}

// CHECK-LABEL: define {{.*}} { x86_fp80, x86_fp80 } @test_ldc(
// CHECK: %[[ROUND_UP:.*]] = getelementptr inbounds{{( nuw)?}} i8, ptr %{{.*}}, i64 15
// CHECK: %[[ALIGNED:.*]] = {{(tail )?}}call{{( align 16)?}} ptr @llvm.ptrmask.p0.i64(ptr {{(nonnull )?}}%[[ROUND_UP]], i64 -16)
// CHECK: %[[NEXT:.*]] = getelementptr{{( inbounds)?}}{{( nuw)?}} i8, ptr %[[ALIGNED]], i64 32
// CHECK: load x86_fp80, ptr %[[ALIGNED]], align 16
// CHECK: getelementptr{{( inbounds)?}}{{( nuw)?}} i8, ptr %[[ALIGNED]], i64 16
// CHECK: load x86_fp80, ptr %{{.*}}, align 16
