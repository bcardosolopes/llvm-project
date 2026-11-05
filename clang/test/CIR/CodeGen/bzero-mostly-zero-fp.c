// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -O1 %s -o - | FileCheck --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 %s -o - | FileCheck --check-prefix=LLVM %s

extern void use(void *);

void float_array(void) {
  float a[9] = {1.0f};
  use(a);
}
// CIR-LABEL: cir.func dso_local @float_array()
// CIR: %[[SZ_FLOAT_ARR:.*]] = cir.const #cir.int<36> : !u64i
// CIR: cir.libc.memset %[[SZ_FLOAT_ARR]] bytes
// CIR: cir.const #cir.fp<1.000000e+00> : !cir.float
// CIR: cir.store {{.*}} : !cir.float, !cir.ptr<!cir.float>
// CIR-NOT: cir.libc.memcpy
// CIR-NOT: cir.get_global @__const.float_array.a
// LLVM-LABEL: define dso_local void @float_array()
// LLVM: call void @llvm.memset{{.*}}i64 36
// LLVM: store float 1.000000e+00, ptr %{{[^ ,]+}}
// LLVM-NOT: @llvm.memcpy
// LLVM-NOT: @__const.float_array.a

void double_array(void) {
  double a[9] = {1.0};
  use(a);
}
// CIR-LABEL: cir.func dso_local @double_array()
// CIR: %[[SZ_DOUBLE_ARR:.*]] = cir.const #cir.int<72> : !u64i
// CIR: cir.libc.memset %[[SZ_DOUBLE_ARR]] bytes
// CIR: cir.const #cir.fp<1.000000e+00> : !cir.double
// CIR: cir.store {{.*}} : !cir.double, !cir.ptr<!cir.double>
// CIR-NOT: cir.libc.memcpy
// CIR-NOT: cir.get_global @__const.double_array.a
// LLVM-LABEL: define dso_local void @double_array()
// LLVM: call void @llvm.memset{{.*}}i64 72
// LLVM: store double 1.000000e+00, ptr %{{[^ ,]+}}
// LLVM-NOT: @llvm.memcpy
// LLVM-NOT: @__const.double_array.a

struct FloatNine {
  float a, b, c, d, e, f, g, h, i;
};

void float_record(void) {
  struct FloatNine s = {1.0f};
  use(&s);
}
// CIR-LABEL: cir.func dso_local @float_record()
// CIR: %[[SZ_FLOAT_REC:.*]] = cir.const #cir.int<36> : !u64i
// CIR: cir.libc.memset %[[SZ_FLOAT_REC]] bytes
// CIR: cir.get_member
// CIR: cir.const #cir.fp<1.000000e+00> : !cir.float
// CIR: cir.store {{.*}} : !cir.float, !cir.ptr<!cir.float>
// CIR-NOT: cir.copy
// CIR-NOT: cir.get_global @__const.float_record.s
// LLVM-LABEL: define dso_local void @float_record()
// LLVM: call void @llvm.memset{{.*}}i64 36
// LLVM: store float 1.000000e+00, ptr %{{[^ ,]+}}
// LLVM-NOT: @llvm.memcpy
// LLVM-NOT: @__const.float_record.s

struct DoubleFive {
  double a, b, c, d, e;
};

void double_record(void) {
  struct DoubleFive s = {1.0};
  use(&s);
}
// CIR-LABEL: cir.func dso_local @double_record()
// CIR: %[[SZ_DOUBLE_REC:.*]] = cir.const #cir.int<40> : !u64i
// CIR: cir.libc.memset %[[SZ_DOUBLE_REC]] bytes
// CIR: cir.get_member
// CIR: cir.const #cir.fp<1.000000e+00> : !cir.double
// CIR: cir.store {{.*}} : !cir.double, !cir.ptr<!cir.double>
// CIR-NOT: cir.copy
// CIR-NOT: cir.get_global @__const.double_record.s
// LLVM-LABEL: define dso_local void @double_record()
// LLVM: call void @llvm.memset{{.*}}i64 40
// LLVM: store double 1.000000e+00, ptr %{{[^ ,]+}}
// LLVM-NOT: @llvm.memcpy
// LLVM-NOT: @__const.double_record.s
