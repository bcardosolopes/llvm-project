// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -O0 -disable-llvm-passes \
// RUN:   -fsanitize=object-size -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that CIR emits object-size sanitizer checks in global variable
// initializer functions (__cxx_global_var_init). Previously CIR skipped
// these checks because sanOpts was not initialized for global init contexts.

struct S {
  int x;
  S() : x(0) {}
  ~S();
};

S &get_s();
S &ref = get_s();

// CHECK: define internal void @__cxx_global_var_init
// CHECK: call i64 @llvm.objectsize.i64.p0
// CHECK: icmp uge i64 {{.*}}, 4
// CHECK: br i1
// CHECK: call void @__ubsan_handle_type_mismatch_v1_abort
// CHECK: store ptr {{.*}}, ptr @ref
