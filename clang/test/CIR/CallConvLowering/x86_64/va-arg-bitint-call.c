// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// Variadic calls with _BitInt(160) args must use byval([24 x i8]) align 8
// at the call site. _BitInt(160) is 24 bytes, too large for registers.
// Without explicit byval on the call, va_arg in the callee reads garbage
// because the pointer is passed instead of the value being copied to the stack.

long long get_from_va(int count, ...);

void test_variadic_bitint160_call(void) {
  _BitInt(160) x = 42, y = 100;
  get_from_va(2, x, y);
}

// CIR-LABEL: @test_variadic_bitint160_call
// CIR:         cir.call @get_from_va({{.*}}) {cir.call_arg_attrs = [{}, {cir.align = 8 : i64, cir.byval = !cir.int<s, 160>, cir.byval_size = 24 : i64, cir.noundef}, {cir.align = 8 : i64, cir.byval = !cir.int<s, 160>, cir.byval_size = 24 : i64, cir.noundef}]}

// LLVM-LABEL: @test_variadic_bitint160_call
// LLVM:         call {{.*}} @get_from_va(i32 {{.*}} 2, ptr {{.*}} byval([24 x i8]) align 8 %{{.*}}, ptr {{.*}} byval([24 x i8]) align 8 %{{.*}})

// OGCG-LABEL: @test_variadic_bitint160_call
// OGCG:         call {{.*}} @get_from_va(i32 {{.*}} 2, ptr {{.*}} byval([24 x i8]) align 8 %{{.*}}, ptr {{.*}} byval([24 x i8]) align 8 %{{.*}})
