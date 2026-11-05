// RUN: %clang_cc1 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that __attribute__((error/warning)) emit dontcall-error/dontcall-warn
// and !srcloc metadata on call sites with i64 encoding.

__attribute__((error("oh no"))) void error_func(void);
__attribute__((warning("be careful"))) void warn_func(void);

void test(void) {
  error_func();
  warn_func();
}

// CHECK: call void @error_func(), !srcloc [[SLOC1:![0-9]+]]
// CHECK: call void @warn_func(), !srcloc [[SLOC2:![0-9]+]]
// CHECK: declare void @warn_func() #[[WARN:[0-9]+]]
// CHECK: declare void @error_func() #[[ERR:[0-9]+]]
// CHECK: attributes #[[WARN]] = {{{.*}}"dontcall-warn"="be careful"
// CHECK: attributes #[[ERR]] = {{{.*}}"dontcall-error"="oh no"
// Verify !srcloc uses i64 (matching OGCG)
// CHECK: [[SLOC1]] = !{i64
// CHECK: [[SLOC2]] = !{i64
