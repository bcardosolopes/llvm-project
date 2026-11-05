// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-llvm \
// RUN:   -O0 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify that __builtin_arm_ldrex and __builtin_arm_strex work with bool,
// float, and double types. CIR must handle BoolType (int_to_bool / bool_to_int
// casts) and float/double (bitcast to/from same-width integer).

bool b;
float f;
double d;

void test_ldrex_bool() {
  b = __builtin_arm_ldrex(&b);
}

void test_strex_bool() {
  __builtin_arm_strex(true, &b);
}

void test_ldrex_float() {
  f = __builtin_arm_ldrex(&f);
}

void test_strex_float(float v) {
  __builtin_arm_strex(v, &f);
}

void test_ldrex_double() {
  d = __builtin_arm_ldrex(&d);
}

void test_strex_double(double v) {
  __builtin_arm_strex(v, &d);
}

// CHECK-LABEL: define {{.*}}void @_Z15test_ldrex_boolv()
// CHECK: call i64 @llvm.aarch64.ldxr.p0(ptr elementtype(i8) @b)
// CHECK: store i8 {{%[0-9]+}}, ptr @b

// CHECK-LABEL: define {{.*}}void @_Z15test_strex_boolv()
// CHECK: call i32 @llvm.aarch64.stxr.p0(i64 1, ptr elementtype(i8) @b)

// CHECK-LABEL: define {{.*}}void @_Z16test_ldrex_floatv()
// CHECK: %[[LDXR:.*]] = call i64 @llvm.aarch64.ldxr.p0(ptr elementtype(i32) @f)
// CHECK: %[[TRUNC:.*]] = trunc i64 %[[LDXR]] to i32
// CHECK: bitcast i32 %[[TRUNC]] to float

// CHECK-LABEL: define {{.*}}void @_Z16test_strex_floatf(float
// CHECK: bitcast float {{.*}} to i32
// CHECK: call i32 @llvm.aarch64.stxr.p0(i64 {{%[0-9]+}}, ptr elementtype(i32) @f)

// CHECK-LABEL: define {{.*}}void @_Z17test_ldrex_doublev()
// CHECK: %[[LDXR2:.*]] = call i64 @llvm.aarch64.ldxr.p0(ptr elementtype(i64) @d)
// CHECK: bitcast i64 %[[LDXR2]] to double

// CHECK-LABEL: define {{.*}}void @_Z17test_strex_doubled(double
// CHECK: bitcast double {{.*}} to i64
// CHECK: call i32 @llvm.aarch64.stxr.p0(i64 {{%[0-9]+}}, ptr elementtype(i64) @d)
