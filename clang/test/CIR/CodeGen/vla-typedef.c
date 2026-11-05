// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that typedef/typealias declarations with variably modified types
// are handled correctly (the VLA size expression must be evaluated).

void use(void *);

void test_vla_typedef(int n) {
  typedef int VLAType[n];
  VLAType a;
  use(a);
}

// CIR: cir.func {{.*}} @test_vla_typedef(%arg0: !s32i
// CIR:   %[[N:.*]] = cir.load {{.*}} : !cir.ptr<!s32i>, !s32i
// CIR:   %[[UNSIGNED:.*]] = cir.cast integral %[[N]] : !s32i -> !u32i
// CIR:   %[[EXT:.*]] = cir.cast integral %[[UNSIGNED]] : !u32i -> !u64i
// CIR:   cir.alloca !s32i, !cir.ptr<!s32i>, %[[EXT]] : !u64i, ["a"]

// LLVM: define {{.*}} @test_vla_typedef(i32 {{.*}} %[[N:.*]])
// LLVM:   %[[EXT:.*]] = zext i32 %{{.*}} to i64
// LLVM:   alloca i32, i64 %[[EXT]], align 16

int side_effect(void);

// The VLA size expression must be evaluated for its side effects
// even when the typedef is never used to declare a variable.
void test_vla_typedef_side_effect(void) {
  typedef int VLAType[side_effect()];
}

// CIR: cir.func {{.*}} @test_vla_typedef_side_effect
// CIR:   cir.call @side_effect() : () -> !s32i

// LLVM: define {{.*}} @test_vla_typedef_side_effect
// LLVM:   call i32 @side_effect()
