// RUN: %clang_cc1 -triple x86_64-linux-gnu -fsanitize=alignment -fno-sanitize-recover=alignment -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify CIR emits __ubsan_handle_alignment_assumption checks for
// __builtin_assume_aligned when -fsanitize=alignment is active.

// CHECK: @[[SRC:.*]] = private unnamed_addr constant [{{[0-9]+}} x i8] c"{{.*[/\\]}}clang/test/CIR/CodeGen/ubsan-alignment-assumption.c\00"
// Verify the type descriptor uses the proper type name, not 'unknown'.
// CHECK: @[[TYPE:.*]] = private unnamed_addr constant { i16, i16, [9 x i8] } { i16 -1, i16 0, [9 x i8] c"'void *'\00" }
// CHECK: @[[INFO1:.*]] = private unnamed_addr global { { ptr, i32, i32 }, { ptr, i32, i32 }, ptr } { { ptr, i32, i32 } { ptr @[[SRC]], i32 {{[0-9]+}}, i32 {{[0-9]+}} }, { ptr, i32, i32 } zeroinitializer, ptr @[[TYPE]] }
// CHECK: @[[INFO2:.*]] = private unnamed_addr global { { ptr, i32, i32 }, { ptr, i32, i32 }, ptr } { { ptr, i32, i32 } { ptr @[[SRC]], i32 {{[0-9]+}}, i32 {{[0-9]+}} }, { ptr, i32, i32 } zeroinitializer, ptr @[[TYPE]] }

void *test_assume_aligned(void *p) {
  // CHECK: define {{.*}} @test_assume_aligned
  // CHECK: ptrtoint
  // CHECK: and i64
  // CHECK: icmp eq i64
  // CHECK: call void @__ubsan_handle_alignment_assumption_abort(
  // CHECK: call void @llvm.assume(i1 true) [ "align"(ptr
  return __builtin_assume_aligned(p, 128);
}

void *test_assume_aligned_with_offset(void *p) {
  // CHECK: define {{.*}} @test_assume_aligned_with_offset
  // CHECK: call void @__ubsan_handle_alignment_assumption_abort(
  // CHECK: call void @llvm.assume(i1 true) [ "align"(ptr
  return __builtin_assume_aligned(p, 64, 8);
}
