// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that _BitInt ext_vector_type with non-power-of-2 element count
// doesn't crash with "Alignment must be power of 2".

using int8_t3 = _BitInt(8) __attribute__((ext_vector_type(3)));

// CHECK: define {{.*}} i32 @_Z4testDv3_DB8_(i32 {{.*}}%{{.*}})
int8_t3 test(int8_t3 p) { return p; }
