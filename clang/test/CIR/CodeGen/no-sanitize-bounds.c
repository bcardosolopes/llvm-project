// RUN: %clang -target x86_64-unknown-linux-gnu -S -emit-llvm -O0 \
// RUN:   -fsanitize=bounds -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang -target x86_64-unknown-linux-gnu -S -emit-llvm -O0 \
// RUN:   -fsanitize=bounds %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s

__attribute__((no_sanitize("bounds"), noinline))
int no_bounds(int i) {
  int a[8];
  return a[i];
}

__attribute__((noinline))
int with_bounds(int i) {
  int a[8];
  return a[i];
}

// CHECK-LABEL: define dso_local i32 @no_bounds(
// CHECK-SAME: ) [[NO_BOUNDS_ATTR:#[0-9]+]] {
// CHECK-NOT: call void @llvm.ubsantrap
// CHECK: ret i32
//
// CHECK-LABEL: define dso_local i32 @with_bounds(
// CHECK: call void @llvm.ubsantrap(i8
// CHECK: unreachable
//
// CHECK: attributes [[NO_BOUNDS_ATTR]] = { {{.*}}nosanitize_bounds{{.*}} }
