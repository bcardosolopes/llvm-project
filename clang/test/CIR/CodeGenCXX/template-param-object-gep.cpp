// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll
//
// Verify that template parameter object with pointer arithmetic on member
// array produces a correct byte-offset GEP instead of crashing with
// "GEP struct index out of bounds".

struct S { char buf[32]; };
template<S s> constexpr const char *end() {
  return s.buf + __builtin_strlen(s.buf);
}
const char *q = end<S{"hello world"}>();

// CHECK: @q = global ptr getelementptr (i8, ptr @_ZTAXtl1S{{.*}}, i64 11)
