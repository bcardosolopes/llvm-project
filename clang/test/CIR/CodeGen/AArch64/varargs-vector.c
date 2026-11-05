// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=AAPCS --implicit-check-not="va_arg ptr"
// RUN: %clang_cc1 -triple arm64-apple-ios7 -target-abi darwinpcs -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=DARWIN --implicit-check-not="va_arg ptr"
// REQUIRES: aarch64-registered-target

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)

typedef __attribute__((ext_vector_type(3))) char char3;
typedef __attribute__((ext_vector_type(19))) char char19;

double varargs_vec_3c(int fixed, ...) {
  va_list ap;
  double sum = fixed;
  va_start(ap, fixed);
  char3 c3 = va_arg(ap, char3);
  sum = sum + c3.x + c3.y;
  va_end(ap);
  return sum;
}

// AAPCS-LABEL: define dso_local double @varargs_vec_3c
// AAPCS:         %[[GR_OFFS_P:.*]] = getelementptr inbounds nuw %struct.__va_list, ptr %{{.*}}, i32 0, i32 3
// AAPCS:         %[[GR_OFFS:.*]] = load i32, ptr %[[GR_OFFS_P]], align 8
// AAPCS:         %[[NEW_GR_OFFS:.*]] = add i32 %[[GR_OFFS]], 8
// AAPCS:         store i32 %[[NEW_GR_OFFS]], ptr %[[GR_OFFS_P]], align 8
// AAPCS:         %[[REG_TOP_P:.*]] = getelementptr inbounds nuw %struct.__va_list, ptr %{{.*}}, i32 0, i32 1
// AAPCS:         %[[LOADVEC:.*]] = load <3 x i8>, ptr %{{.*}}, align 4

// DARWIN-LABEL: define double @varargs_vec_3c
// DARWIN:         %[[AP_CUR:.*]] = load ptr, ptr %{{.*}}, align 8
// DARWIN:         %[[AP_NEXT:.*]] = getelementptr inbounds i8, ptr %[[AP_CUR]], i64 8
// DARWIN:         store ptr %[[AP_NEXT]], ptr %{{.*}}, align 8
// DARWIN:         %[[LOADVEC:.*]] = load <3 x i8>, ptr %[[AP_CUR]], align 4

double varargs_vec_19c(int fixed, ...) {
  va_list ap;
  double sum = fixed;
  va_start(ap, fixed);
  char19 c19 = va_arg(ap, char19);
  sum = sum + c19.x + c19.y;
  va_end(ap);
  return sum;
}

// AAPCS-LABEL: define dso_local double @varargs_vec_19c
// AAPCS:         %[[GR_OFFS_P:.*]] = getelementptr inbounds nuw %struct.__va_list, ptr %{{.*}}, i32 0, i32 3
// AAPCS:         %[[GR_OFFS:.*]] = load i32, ptr %[[GR_OFFS_P]], align 8
// AAPCS:         %[[NEW_GR_OFFS:.*]] = add i32 %[[GR_OFFS]], 8
// AAPCS:         store i32 %[[NEW_GR_OFFS]], ptr %[[GR_OFFS_P]], align 8
// AAPCS:         %[[ARGPTR:.*]] = load ptr, ptr %{{.*}}, align 8
// AAPCS:         %[[LOADVEC:.*]] = load <19 x i8>, ptr %{{.*}}, align 16

// DARWIN-LABEL: define double @varargs_vec_19c
// DARWIN:         %[[AP_CUR:.*]] = load ptr, ptr %{{.*}}, align 8
// DARWIN:         %[[AP_NEXT:.*]] = getelementptr inbounds i8, ptr %[[AP_CUR]], i64 8
// DARWIN:         store ptr %[[AP_NEXT]], ptr %{{.*}}, align 8
// DARWIN:         %[[ARGPTR:.*]] = load ptr, ptr %[[AP_CUR]], align 8
// DARWIN:         %[[LOADVEC:.*]] = load <19 x i8>, ptr %{{.*}}, align 16
