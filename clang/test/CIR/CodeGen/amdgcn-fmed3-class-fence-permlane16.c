// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// These AMDGPU builtins need special CIR handling. Classic ref:
// clang/lib/CodeGen/TargetBuiltins/AMDGPU.cpp.
//   fmed3f     -> llvm.amdgcn.fmed3.f32 (overloaded on the operand type)
//   classf     -> llvm.amdgcn.class.f32 (returns i1, zext to the result type)
//   fence      -> a fence with the parsed memory order + sync scope
//   permlane16 -> llvm.amdgcn.permlane16.i32 (overloaded on the first operand)

float test_fmed3f(float a, float b, float c) {
  return __builtin_amdgcn_fmed3f(a, b, c);
}

// CIR-LABEL: @test_fmed3f
// CIR: cir.call_llvm_intrinsic "amdgcn.fmed3" {{.*}} : (!cir.float, !cir.float, !cir.float) -> !cir.float
// LLVM-LABEL: @test_fmed3f
// LLVM: call float @llvm.amdgcn.fmed3.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})
// OGCG-LABEL: @test_fmed3f
// OGCG: call float @llvm.amdgcn.fmed3.f32(float %{{.*}}, float %{{.*}}, float %{{.*}})

int test_classf(float a, int b) {
  return __builtin_amdgcn_classf(a, b);
}

// CIR-LABEL: @test_classf
// CIR: %[[CLS:.*]] = cir.call_llvm_intrinsic "amdgcn.class" {{.*}} : (!cir.float, !s32i) -> !cir.bool
// CIR: cir.cast bool_to_int %[[CLS]] : !cir.bool -> !s32i
// LLVM-LABEL: @test_classf
// LLVM: %[[C:.*]] = call i1 @llvm.amdgcn.class.f32(float %{{.*}}, i32 %{{.*}})
// LLVM: zext i1 %[[C]] to i32
// OGCG-LABEL: @test_classf
// OGCG: %[[C:.*]] = call i1 @llvm.amdgcn.class.f32(float %{{.*}}, i32 %{{.*}})
// OGCG: zext i1 %[[C]] to i32

void test_fence(void) {
  __builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "workgroup");
}

// CIR-LABEL: @test_fence
// CIR: cir.atomic.fence syncscope(workgroup) seq_cst
// LLVM-LABEL: @test_fence
// LLVM: fence syncscope("workgroup") seq_cst
// OGCG-LABEL: @test_fence
// OGCG: fence syncscope("workgroup") seq_cst

unsigned test_permlane16(unsigned a, unsigned b, unsigned c, unsigned d) {
  return __builtin_amdgcn_permlane16(a, b, c, d, 0, 0);
}

// CIR-LABEL: @test_permlane16
// CIR: cir.call_llvm_intrinsic "amdgcn.permlane16" {{.*}} : (!u32i, !u32i, !u32i, !u32i, !cir.bool, !cir.bool) -> !u32i
// LLVM-LABEL: @test_permlane16
// LLVM: call i32 @llvm.amdgcn.permlane16.i32(i32 %{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i1 false, i1 false)
// OGCG-LABEL: @test_permlane16
// OGCG: call i32 @llvm.amdgcn.permlane16.i32(i32 %{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i1 false, i1 false)
