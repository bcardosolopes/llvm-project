// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -disable-llvm-passes \
// RUN:   -fsanitize=array-bounds,signed-integer-overflow \
// RUN:   -fsanitize-recover=array-bounds,signed-integer-overflow \
// RUN:   -fclangir -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck %s --input-file=%t.cir.ll
//
// The array-bounds out-of-bounds handler builds a type descriptor for the
// index type. It must reuse the shared type-descriptor cache instead of
// emitting a fresh global, so an index type that also appears in another check
// (here a signed-overflow check on the same 'int' type) is emitted only once,
// matching classic clang.
// RUN: grep "private unnamed_addr constant { i16, i16, \\[6 x i8\\] } { i16 0, i16 11, \\[6 x i8\\] c\"'int'\\\\00\" }" %t.cir.ll | count 1
//
// Classic clang likewise emits the descriptor exactly once.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -disable-llvm-passes \
// RUN:   -fsanitize=array-bounds,signed-integer-overflow \
// RUN:   -fsanitize-recover=array-bounds,signed-integer-overflow \
// RUN:   -emit-llvm -o %t.ogcg.ll %s
// RUN: grep "private unnamed_addr constant { i16, i16, \\[6 x i8\\] } { i16 0, i16 11, \\[6 x i8\\] c\"'int'\\\\00\" }" %t.ogcg.ll | count 1

int add(int a, int b) { return a + b; }

int idx(int i) {
  int arr[8];
  return arr[i];
}

// CHECK: @{{[0-9]+}} = private unnamed_addr constant { i16, i16, [6 x i8] } { i16 0, i16 11, [6 x i8] c"'int'\00" }
// CHECK: call void @__ubsan_handle_out_of_bounds
