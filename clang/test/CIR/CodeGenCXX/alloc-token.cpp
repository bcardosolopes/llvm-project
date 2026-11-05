// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fexceptions -fcxx-exceptions -fclangir -fsanitize=alloc-token -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

typedef __typeof(sizeof(int)) size_t;

extern "C" void *malloc(size_t size) __attribute__((malloc));

void *sink;

class ForwardDecl;

// CHECK-LABEL: define dso_local void @_Z21test_malloc_like_castv(
// CHECK: call noalias ptr @malloc(i64 noundef 64) #{{[0-9]+}}, !alloc_token [[META_INT:![0-9]+]]
// CHECK: call noalias ptr @malloc(i64 noundef 64) #{{[0-9]+}}, !alloc_token [[META_INT]]
// CHECK: call noalias ptr @malloc(i64 noundef 64) #{{[0-9]+}}{{$}}
void test_malloc_like_cast() {
  sink = (int *)malloc(64);
  sink = reinterpret_cast<int *>(malloc(64));
  sink = reinterpret_cast<ForwardDecl *>(malloc(64));
}

// CHECK-LABEL: define dso_local void @_Z17test_operator_newv(
// CHECK: call noalias noundef nonnull ptr @_Znwm(i64 noundef 4) #{{[0-9]+}}, !alloc_token [[META_INT]]
// CHECK: call noalias noundef nonnull ptr @_Znwm(i64 noundef 4) #{{[0-9]+}}, !alloc_token [[META_INT]]
void test_operator_new() {
  sink = __builtin_operator_new(sizeof(int));
  sink = ::operator new(sizeof(int));
}

// CHECK-LABEL: define dso_local noundef ptr @_Z8test_newv(
// CHECK: call noalias noundef nonnull ptr @_Znwm(i64 noundef 4) #{{[0-9]+}}, !alloc_token [[META_INT]]
int *test_new() {
  return new int;
}

struct TestClass {
  TestClass();
  virtual ~TestClass();
  int data[16];
};

void may_throw();

// CHECK-LABEL: define dso_local noundef ptr @_Z27test_exception_handling_newv(
// CHECK: invoke noalias noundef nonnull ptr @_Znwm(i64 noundef 72) #{{[0-9]+}}
// CHECK-NEXT: to label %{{[0-9]+}} unwind label %{{[0-9]+}}, !alloc_token [[META_TESTCLASS:![0-9]+]]
TestClass *test_exception_handling_new() {
  try {
    TestClass *obj = new TestClass();
    may_throw();
    return obj;
  } catch (...) {
    return nullptr;
  }
}

// CHECK: [[META_INT]] = !{!"int", i1 false}
// CHECK: [[META_TESTCLASS]] = !{!"TestClass", i1 true}
