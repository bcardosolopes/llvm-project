// RUN: %clang_cc1 -triple x86_64-apple-darwin -fblocks -fclangir -emit-cir %s -o - | FileCheck %s

// Noncapturing block expressions are now lowered as global block literals.

void test() {
  void (^block)(void) = ^{};
}

// CHECK: cir.global "private"  constant internal  @___Z4testv_block_invoke.literal
// CHECK: cir.func no_inline optnone internal private @___Z4testv_block_invoke.invoke
// CHECK: %{{.*}} = cir.const #cir.global_view<@___Z4testv_block_invoke.literal> : !cir.ptr<!rec_anon_struct
