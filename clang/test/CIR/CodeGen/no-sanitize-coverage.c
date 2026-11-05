// RUN: %clang -target x86_64-unknown-linux-gnu -S -emit-llvm -O0 \
// RUN:   -fsanitize-coverage=trace-pc,trace-cmp -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang -target x86_64-unknown-linux-gnu -S -emit-llvm -O0 \
// RUN:   -fsanitize-coverage=trace-pc,trace-cmp %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s

__attribute__((no_sanitize("coverage"), noinline))
int no_coverage(int n) {
  if (n == 42)
    return n + 1;
  return n - 1;
}

__attribute__((noinline))
int with_coverage(int n) {
  if (n == 42)
    return n + 1;
  return n - 1;
}

// CHECK-LABEL: define dso_local i32 @no_coverage(
// CHECK-SAME: ) [[NO_COVERAGE_ATTR:#[0-9]+]] {
// CHECK-NOT: call void @__sanitizer_cov_trace_pc()
// CHECK-NOT: call void @__sanitizer_cov_trace_const_cmp4
// CHECK: ret i32
//
// CHECK-LABEL: define dso_local i32 @with_coverage(
// CHECK: call void @__sanitizer_cov_trace_pc()
// CHECK: call void @__sanitizer_cov_trace_const_cmp4(i32 42, i32 %{{.*}})
//
// CHECK: attributes [[NO_COVERAGE_ATTR]] = { {{.*}}nosanitize_coverage{{.*}} }
