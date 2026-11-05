// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512bw -target-feature +avx512vl \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512bw -target-feature +avx512vl \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef char __v16qi __attribute__((__vector_size__(16)));
typedef short __v8hi __attribute__((__vector_size__(16)));
typedef int __v4si __attribute__((__vector_size__(16)));
typedef long long __v2di __attribute__((__vector_size__(16)));

// Signed byte compare: eq (cc=0)
unsigned short test_cmpb128_eq(__v16qi a, __v16qi b) {
  return __builtin_ia32_cmpb128_mask(a, b, 0, -1);
}
// CHECK-LABEL: @test_cmpb128_eq
// CHECK: icmp eq <16 x i8>
// OGCG-LABEL: @test_cmpb128_eq
// OGCG: icmp eq <16 x i8>

// Signed byte compare: lt (cc=1)
unsigned short test_cmpb128_slt(__v16qi a, __v16qi b) {
  return __builtin_ia32_cmpb128_mask(a, b, 1, -1);
}
// CHECK-LABEL: @test_cmpb128_slt
// CHECK: icmp slt <16 x i8>
// OGCG-LABEL: @test_cmpb128_slt
// OGCG: icmp slt <16 x i8>

// Signed byte compare: gt (cc=6)
unsigned short test_cmpb128_sgt(__v16qi a, __v16qi b) {
  return __builtin_ia32_cmpb128_mask(a, b, 6, -1);
}
// CHECK-LABEL: @test_cmpb128_sgt
// CHECK: icmp sgt <16 x i8>
// OGCG-LABEL: @test_cmpb128_sgt
// OGCG: icmp sgt <16 x i8>

// Unsigned byte compare: lt (cc=1) — should produce icmp ult
unsigned short test_ucmpb128_ult(__v16qi a, __v16qi b) {
  return __builtin_ia32_ucmpb128_mask(a, b, 1, -1);
}
// CHECK-LABEL: @test_ucmpb128_ult
// CHECK: icmp ult <16 x i8>
// OGCG-LABEL: @test_ucmpb128_ult
// OGCG: icmp ult <16 x i8>

// Unsigned byte compare: gt (cc=6) — should produce icmp ugt
unsigned short test_ucmpb128_ugt(__v16qi a, __v16qi b) {
  return __builtin_ia32_ucmpb128_mask(a, b, 6, -1);
}
// CHECK-LABEL: @test_ucmpb128_ugt
// CHECK: icmp ugt <16 x i8>
// OGCG-LABEL: @test_ucmpb128_ugt
// OGCG: icmp ugt <16 x i8>

// Unsigned byte compare: le (cc=2) — should produce icmp ule
unsigned short test_ucmpb128_ule(__v16qi a, __v16qi b) {
  return __builtin_ia32_ucmpb128_mask(a, b, 2, -1);
}
// CHECK-LABEL: @test_ucmpb128_ule
// CHECK: icmp ule <16 x i8>
// OGCG-LABEL: @test_ucmpb128_ule
// OGCG: icmp ule <16 x i8>

// Unsigned byte compare: eq (cc=0) — eq is sign-agnostic
unsigned short test_ucmpb128_eq(__v16qi a, __v16qi b) {
  return __builtin_ia32_ucmpb128_mask(a, b, 0, -1);
}
// CHECK-LABEL: @test_ucmpb128_eq
// CHECK: icmp eq <16 x i8>
// OGCG-LABEL: @test_ucmpb128_eq
// OGCG: icmp eq <16 x i8>

// Signed dword compare: le (cc=2)
unsigned char test_cmpd128_sle(__v4si a, __v4si b) {
  return __builtin_ia32_cmpd128_mask(a, b, 2, -1);
}
// CHECK-LABEL: @test_cmpd128_sle
// CHECK: icmp sle <4 x i32>
// OGCG-LABEL: @test_cmpd128_sle
// OGCG: icmp sle <4 x i32>

// Unsigned dword compare: ge (cc=5)
unsigned char test_ucmpd128_uge(__v4si a, __v4si b) {
  return __builtin_ia32_ucmpd128_mask(a, b, 5, -1);
}
// CHECK-LABEL: @test_ucmpd128_uge
// CHECK: icmp uge <4 x i32>
// OGCG-LABEL: @test_ucmpd128_uge
// OGCG: icmp uge <4 x i32>

// cc=3 (FALSE): all-false result, no comparison needed
unsigned short test_cmpb128_false(__v16qi a, __v16qi b) {
  return __builtin_ia32_cmpb128_mask(a, b, 3, -1);
}
// CHECK-LABEL: @test_cmpb128_false
// CHECK: ret i16 0
// OGCG-LABEL: @test_cmpb128_false
// OGCG: ret i16 0

// cc=7 (TRUE): all-true result, no comparison needed
unsigned short test_cmpb128_true(__v16qi a, __v16qi b) {
  return __builtin_ia32_cmpb128_mask(a, b, 7, -1);
}
// CHECK-LABEL: @test_cmpb128_true
// CHECK: ret i16 -1
// OGCG-LABEL: @test_cmpb128_true
// OGCG: ret i16 -1
