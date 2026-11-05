// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// __builtin_annotation annotates an integer value with a string.
// It is an identity function at runtime.

// Verify annotation string globals are created.
// LLVM: @{{.*}}annotation{{.*}} = private unnamed_addr constant [14 x i8] c"my_annotation\00", section "llvm.metadata"
// OGCG: @{{.*}} = private unnamed_addr constant [14 x i8] c"my_annotation\00", section "llvm.metadata"

int annotate_int(int x) {
  return __builtin_annotation(x, "my_annotation");
}

// CIR-LABEL: @_Z12annotate_inti
// CIR: cir.builtin_annotation %{{.*}}, "my_annotation" : !s32i

// LLVM-LABEL: @_Z12annotate_inti
// LLVM: call i32 @llvm.annotation.i32

// OGCG-LABEL: @_Z12annotate_inti
// OGCG: call i32 @llvm.annotation.i32

long long annotate_longlong(long long x) {
  return __builtin_annotation(x, "my_annotation");
}

// CIR-LABEL: @_Z17annotate_longlongx
// CIR: cir.builtin_annotation %{{.*}}, "my_annotation" : !s64i

// LLVM-LABEL: @_Z17annotate_longlongx
// LLVM: call i64 @llvm.annotation.i64

// OGCG-LABEL: @_Z17annotate_longlongx
// OGCG: call i64 @llvm.annotation.i64

short annotate_short(short x) {
  return __builtin_annotation(x, "my_annotation");
}

// CIR-LABEL: @_Z14annotate_shorts
// CIR: cir.builtin_annotation %{{.*}}, "my_annotation" : !s16i

// LLVM-LABEL: @_Z14annotate_shorts
// LLVM: call i16 @llvm.annotation.i16

// OGCG-LABEL: @_Z14annotate_shorts
// OGCG: call i16 @llvm.annotation.i16

char annotate_char(char x) {
  return __builtin_annotation(x, "my_annotation");
}

// CIR-LABEL: @_Z13annotate_charc
// CIR: cir.builtin_annotation %{{.*}}, "my_annotation" : !s8i

// LLVM-LABEL: @_Z13annotate_charc
// LLVM: call i8 @llvm.annotation.i8

// OGCG-LABEL: @_Z13annotate_charc
// OGCG: call i8 @llvm.annotation.i8
