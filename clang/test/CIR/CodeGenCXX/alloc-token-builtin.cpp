// To test IR generation of the builtin without evaluating the LLVM intrinsic,
// we set the mode to a stateful mode, which prohibits constant evaluation.
// RUN: %clang_cc1 -triple x86_64-linux-gnu -Werror -std=c++20 -fclangir -emit-llvm -falloc-token-mode=random -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,CODEGEN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -Werror -std=c++20 -fclangir -emit-llvm -falloc-token-max=2 -o - %s | FileCheck %s --check-prefixes=CHECK,LOWER

extern "C" void *my_malloc(unsigned long, unsigned long);

struct WithPtr {
  int a;
  char *buf;
};

int unevaluated_fn();

// CHECK-LABEL: @_Z16test_builtin_intv(
// CODEGEN: call i64 @llvm.alloc.token.id.i64(metadata ![[META_INT:[0-9]+]])
// LOWER: ret i64 0
unsigned long test_builtin_int() {
  return __builtin_infer_alloc_token(sizeof(1));
}

// CHECK-LABEL: @_Z24test_builtin_unevaluatedv(
// CHECK-NOT: call{{.*}}unevaluated_fn
// CODEGEN: call i64 @llvm.alloc.token.id.i64(metadata ![[META_INT]])
// LOWER: ret i64 0
unsigned long test_builtin_unevaluated() {
  return __builtin_infer_alloc_token(sizeof(int) * unevaluated_fn());
}

// CHECK-LABEL: @_Z25test_builtin_struct_w_ptrv(
// CODEGEN: call i64 @llvm.alloc.token.id.i64(metadata ![[META_WITHPTR:[0-9]+]])
// LOWER: ret i64 1
unsigned long test_builtin_struct_w_ptr() {
  return __builtin_infer_alloc_token(sizeof(WithPtr), 123);
}

// CHECK-LABEL: @_Z36test_builtin_unsequenced_unevaluatedi(
// CHECK: add nsw
// CHECK-NOT: add nsw
// CODEGEN: %[[REG:[0-9]+]] = call i64 @llvm.alloc.token.id.i64(metadata ![[META_UNKNOWN:[0-9]+]])
// CODEGEN: call{{.*}}@my_malloc({{.*}}, i64 noundef %[[REG]])
// LOWER: call{{.*}}@my_malloc({{.*}}, i64 noundef 0)
void test_builtin_unsequenced_unevaluated(int x) {
  my_malloc(++x, __builtin_infer_alloc_token(++x));
}

// CHECK-LABEL: @_Z20test_builtin_unknownv(
// CODEGEN: call i64 @llvm.alloc.token.id.i64(metadata ![[META_UNKNOWN]])
// LOWER: ret i64 0
unsigned long test_builtin_unknown() {
  return __builtin_infer_alloc_token(4096);
}

// CODEGEN: !{{[0-9]+}} = !{i32 1, !"alloc-token-mode", !"random"}
// LOWER: !{{[0-9]+}} = !{i32 1, !"alloc-token-max", i64 2}
// CODEGEN: ![[META_INT]] = !{!"int", i1 false}
// CODEGEN: ![[META_WITHPTR]] = !{!"WithPtr", i1 true}
// CODEGEN: ![[META_UNKNOWN]] = !{}
