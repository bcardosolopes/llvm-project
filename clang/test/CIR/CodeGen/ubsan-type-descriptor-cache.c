// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -disable-llvm-passes \
// RUN:   -fsanitize=alignment,null,shift-base,shift-exponent,float-cast-overflow \
// RUN:   -fsanitize-recover=alignment,null,shift-base,shift-exponent,float-cast-overflow \
// RUN:   -fclangir -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck %s --input-file=%t.cir.ll --check-prefix=CHECK
// RUN: grep "private unnamed_addr constant { i16, i16, \\[6 x i8\\] } { i16 0, i16 11, \\[6 x i8\\] c\"'int'\\\\00\" }" %t.cir.ll | count 1
// RUN: grep "private unnamed_addr constant { i16, i16, \\[8 x i8\\] } { i16 1, i16 32, \\[8 x i8\\] c\"'float'\\\\00\" }" %t.cir.ll | count 1
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -disable-llvm-passes \
// RUN:   -fsanitize=alignment,null,shift-base,shift-exponent,float-cast-overflow \
// RUN:   -fsanitize-recover=alignment,null,shift-base,shift-exponent,float-cast-overflow \
// RUN:   -fclangir -emit-cir -o %t.cir %s
// RUN: cir-translate -cir-to-llvmir %t.cir -o %t.translate.ll
// RUN: FileCheck %s --input-file=%t.translate.ll --check-prefix=CHECK
// RUN: grep "private unnamed_addr constant { i16, i16, \\[6 x i8\\] } { i16 0, i16 11, \\[6 x i8\\] c\"'int'\\\\00\" }" %t.translate.ll | count 1
// RUN: grep "private unnamed_addr constant { i16, i16, \\[8 x i8\\] } { i16 1, i16 32, \\[8 x i8\\] c\"'float'\\\\00\" }" %t.translate.ll | count 1
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -disable-llvm-passes \
// RUN:   -fsanitize=alignment,null,shift-base,shift-exponent,float-cast-overflow \
// RUN:   -fsanitize-recover=alignment,null,shift-base,shift-exponent,float-cast-overflow \
// RUN:   -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=CHECK
// RUN: grep "private unnamed_addr constant { i16, i16, \\[6 x i8\\] } { i16 0, i16 11, \\[6 x i8\\] c\"'int'\\\\00\" }" %t.ogcg.ll | count 1
// RUN: grep "private unnamed_addr constant { i16, i16, \\[8 x i8\\] } { i16 1, i16 32, \\[8 x i8\\] c\"'float'\\\\00\" }" %t.ogcg.ll | count 1

// CHECK: @[[INT:[^ ]+]] = private unnamed_addr constant { i16, i16, [6 x i8] } { i16 0, i16 11, [6 x i8] c"'int'\00" }
// CHECK: @[[LOAD_INFO:[^ ]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, i8, i8 } { { ptr, i32, i32 } { ptr @{{.*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, ptr @[[INT]], i8 2, i8 0 }
// CHECK: @[[STORE_INFO:[^ ]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, i8, i8 } { { ptr, i32, i32 } { ptr @{{.*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, ptr @[[INT]], i8 2, i8 1 }
// CHECK: @[[SHIFT_INFO:[^ ]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr } { { ptr, i32, i32 } { ptr @{{.*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, ptr @[[INT]], ptr @[[INT]] }
// CHECK: @[[FLOAT:[^ ]+]] = private unnamed_addr constant { i16, i16, [8 x i8] } { i16 1, i16 32, [8 x i8] c"'float'\00" }
// CHECK: @[[CAST_INFO:[^ ]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr } { { ptr, i32, i32 } { ptr @{{.*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, ptr @[[FLOAT]], ptr @[[INT]] }
// CHECK: @[[UINT:[^ ]+]] = private unnamed_addr constant { i16, i16, [15 x i8] } { i16 0, i16 10, [15 x i8] c"'unsigned int'\00" }
// CHECK: @[[CAST_INFO2:[^ ]+]] = private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr } { { ptr, i32, i32 } { ptr @{{.*}}, i32 {{[0-9]+}}, i32 {{[0-9]+}} }, ptr @[[FLOAT]], ptr @[[UINT]] }

int load_int(int *p) { return *p; }

void store_int(int *p, int v) { *p = v; }

int shl_int(int a, int b) { return a << b; }

int float_to_int(float f) { return f; }

unsigned float_to_uint(float f) { return f; }
