// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fsanitize=alloc-token -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang --target=x86_64-unknown-linux-gnu -fclangir -fsanitize=alloc-token -c %s -o /dev/null

typedef __typeof(sizeof(int)) size_t;

void *malloc(size_t size) __attribute__((malloc));

void *sink;

// CHECK: ; Function Attrs: {{.*}}sanitize_alloc_token
// CHECK-LABEL: define dso_local void @test_malloc_like(
// CHECK: call noalias ptr @malloc(i64 noundef 4) #{{[0-9]+}}, !alloc_token [[META_INT:![0-9]+]]
void test_malloc_like() {
  sink = malloc(sizeof(int));
}

// CHECK-LABEL: define dso_local void @test_no_sanitize(
// CHECK: call noalias ptr @malloc(i64 noundef 4) #{{[0-9]+}}{{$}}
void test_no_sanitize() __attribute__((no_sanitize("alloc-token"))) {
  sink = malloc(sizeof(int));
}

// CHECK: [[META_INT]] = !{!"int", i1 false}
