// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// cir.byte_swap now accepts signed integer types
// __builtin_bswapg is sign-agnostic

int test(int x) { return __builtin_bswapg(x); }

// CHECK-LABEL: @test(
// CHECK:         %{{.+}} = call i32 @llvm.bswap.i32(i32 %{{.+}})
