// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir
//
// Verify that non-default calling conventions are properly represented in CIR.
// Previously, any calling convention other than C, DeviceKernel, or
// SpirFunction crashed with llvm_unreachable.

void __attribute__((preserve_most)) preserve_most_func(int x);
void __attribute__((preserve_all)) preserve_all_func(int x);

void test(void) {
  preserve_most_func(42);
  preserve_all_func(42);
}

// CHECK: cir.func {{.*}} @test
// CHECK:   cir.call @preserve_most_func({{.*}}) : ({{.*}}) -> () cc(preserve_most)
// CHECK:   cir.call @preserve_all_func({{.*}}) : ({{.*}}) -> () cc(preserve_all)
// CHECK: cir.func private @preserve_all_func({{.*}}) cc(preserve_all)
// CHECK: cir.func private @preserve_most_func({{.*}}) cc(preserve_most)
