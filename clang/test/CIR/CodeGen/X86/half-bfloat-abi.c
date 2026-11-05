// See also clang/test/CodeGen/X86/avx512fp16-abi.c and bfloat-abi.c
// RUN: %clang_cc1 -triple x86_64-linux -target-feature +avx512fp16 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck %s --input-file=%t.cir.ll --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-linux -target-feature +avx512fp16 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=CHECK

// Test that small half/bfloat+float structs are coerced to <4 x half> not scalar.

struct halffloat {
  _Float16 a;
  float b;
};

// CHECK: define {{.*}}<4 x half> @hf_ret(
struct halffloat hf_ret(_Float16 a, float b) {
  struct halffloat s;
  s.a = a;
  s.b = b;
  return s;
}

struct bfloatfloat {
  __bf16 a;
  float b;
};

// CHECK: define {{.*}}<4 x half> @bf_ret(
struct bfloatfloat bf_ret(__bf16 a, float b) {
  struct bfloatfloat s;
  s.a = a;
  s.b = b;
  return s;
}
