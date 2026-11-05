// RUN: %clang_cc1 -opaque-pointers -triple arm64ec-windows-msvc -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=COMMON
// RUN: %clang_cc1 -opaque-pointers -triple arm64ec-windows-msvc -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=COMMON

// arm64ec follows the Microsoft varargs ABI. Direct-CIR must match OGCG for
// Win64 va_arg pointer loads, caller-side temporary copies of non-byval
// indirect lvalue varargs, and Windows null vararg widening to intptr_t.

typedef struct { float x[2]; } A;
typedef struct { char x[6]; } B;
typedef struct { float x[4]; } C;

void f(A a, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, a);
  float x = __builtin_va_arg(args, A).x[0];
  float y = __builtin_va_arg(args, B).x[0];
  float z = __builtin_va_arg(args, C).x[0];
}

void g(A a, B b, C c) { f(a, b, c); }

void sink(int fixed, ...);
void null_vararg(void) { sink(0, 0); }

// COMMON-LABEL: @f(
// COMMON: call void @llvm.va_start.p0(ptr [[ARGS:%.*]])
// COMMON: store float {{.*}}, ptr %{{.*}}, align 4
// COMMON: [[ARGP_CUR3:%.*]] = load ptr, ptr [[ARGS]], align 8
// COMMON-NEXT: [[ARGP_NEXT4:%.*]] = getelementptr inbounds i8, ptr [[ARGP_CUR3]], i64 8
// COMMON-NEXT: store ptr [[ARGP_NEXT4]], ptr [[ARGS]], align 8
// COMMON-NEXT: [[TMP1:%.*]] = load ptr, ptr [[ARGP_CUR3]], align 8
// COMMON-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 1 [[REF_TMP2:%.*]], ptr align 1 [[TMP1]], i64 6, i1 false)
// COMMON: store float {{.*}}, ptr %{{.*}}, align 4
// COMMON: [[ARGP_CUR8:%.*]] = load ptr, ptr [[ARGS]], align 8
// COMMON-NEXT: [[ARGP_NEXT9:%.*]] = getelementptr inbounds i8, ptr [[ARGP_CUR8]], i64 8
// COMMON-NEXT: store ptr [[ARGP_NEXT9]], ptr [[ARGS]], align 8
// COMMON-NEXT: [[TMP3:%.*]] = load ptr, ptr [[ARGP_CUR8]], align 8
// COMMON-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 4 [[REF_TMP7:%.*]], ptr align 4 [[TMP3]], i64 16, i1 false)

// COMMON-LABEL: @g(
// COMMON: [[B:%.*]] = alloca [[STRUCT_B:%.*]],{{.*}} align 1
// COMMON: [[C:%.*]] = alloca [[STRUCT_C:%.*]],{{.*}} align 4
// COMMON: [[TMP0:%.*]] = load i64, ptr [[COERCE_DIVE3:%.*]], align 4
// COMMON: call void @llvm.memcpy.p0.p0.i64(ptr align 1 [[B_COPY:%.*]], ptr align 1 [[B]], i64 6, i1 false)
// COMMON: call void @llvm.memcpy.p0.p0.i64(ptr align 4 [[C_COPY:%.*]], ptr align 4 [[C]], i64 16, i1 false)
// COMMON: call void (i64, ...) @f(i64 [[TMP0]], ptr dead_on_return noundef [[B_COPY]], ptr dead_on_return noundef [[C_COPY]])

// COMMON-LABEL: @null_vararg(
// COMMON: call void (i32, ...) @sink(i32 noundef 0, i64 noundef 0)
