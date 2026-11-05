// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that boolean vector return types are correctly coerced to integer types
// matching the number of boolean elements (packed 1-bit-per-element).

using v1i = int [[clang::ext_vector_type(1)]];
using v1b = bool [[clang::ext_vector_type(1)]];
using v8i = int [[clang::ext_vector_type(8)]];
using v8b = bool [[clang::ext_vector_type(8)]];
using v16i = short [[clang::ext_vector_type(16)]];
using v16b = bool [[clang::ext_vector_type(16)]];
using v32i = char [[clang::ext_vector_type(32)]];
using v32b = bool [[clang::ext_vector_type(32)]];

v1b foo(v1i v) { return v; }
// LLVM: define dso_local noundef i8 @_Z3fooDv1_i(
// OGCG: define dso_local noundef i8 @_Z3fooDv1_i(

v8b foo(v8i v) { return v; }
// LLVM: define dso_local noundef i8 @_Z3fooDv8_i(
// OGCG: define dso_local noundef i8 @_Z3fooDv8_i(

v16b foo(v16i v) { return v; }
// LLVM: define dso_local noundef i16 @_Z3fooDv16_s(
// OGCG: define dso_local noundef i16 @_Z3fooDv16_s(

v32b foo(v32i v) { return v; }
// LLVM: define dso_local noundef i32 @_Z3fooDv32_c(
// OGCG: define dso_local noundef i32 @_Z3fooDv32_c(
