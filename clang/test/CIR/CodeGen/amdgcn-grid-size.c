// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// __builtin_amdgcn_grid_size_x/y/z read the grid size out of the HSA
// kernel_dispatch_packet via the dispatch pointer. Classic ref:
// clang/lib/CodeGen/TargetBuiltins/AMDGPU.cpp EmitAMDGPUGridSize:
//   %dp = call align 4 dereferenceable(64) ptr addrspace(4)
//             @llvm.amdgcn.dispatch.ptr()
//   %gep = getelementptr i8, ptr addrspace(4) %dp, i32 (12 + Index*4)
//   %v = load i32, ptr addrspace(4) %gep, align 4, !range !{i32 1, i32 0},
//             !invariant.load
// The align 4 / dereferenceable(64) call-site return attributes are attached via
// the opt-in res_attrs on cir.call_llvm_intrinsic.

unsigned test_grid_size_x(void) {
  return __builtin_amdgcn_grid_size_x();
}

// CIR-LABEL: @test_grid_size_x
// CIR: %[[DP:.*]] = cir.call_llvm_intrinsic "amdgcn.dispatch.ptr"  : () -> !cir.ptr<!s8i, target_address_space(4)> {res_attrs = [{llvm.align = 4 : i64, llvm.dereferenceable = 64 : i64}]}
// CIR: %[[OFF:.*]] = cir.const #cir.int<12>
// CIR: %[[GEP:.*]] = cir.ptr_stride %[[DP]], %[[OFF]] : (!cir.ptr<!s8i, target_address_space(4)>, !s32i) -> !cir.ptr<!s8i, target_address_space(4)>
// CIR: cir.load align(4) {{.*}} : !cir.ptr<!u32i, target_address_space(4)>, !u32i {cir.invariant_load, cir.range = [1 : i32, 0 : i32]}
// LLVM-LABEL: @test_grid_size_x
// LLVM: %[[DP:.*]] = call align 4 dereferenceable(64) ptr addrspace(4) @llvm.amdgcn.dispatch.ptr()
// LLVM: %[[GEP:.*]] = getelementptr i8, ptr addrspace(4) %[[DP]], i64 12
// LLVM: load i32, ptr addrspace(4) %[[GEP]], align 4, !range ![[RANGE:[0-9]+]], !invariant.load ![[INV:[0-9]+]]
// OGCG-LABEL: @test_grid_size_x
// OGCG: %[[DP:.*]] = call align 4 dereferenceable(64) ptr addrspace(4) @llvm.amdgcn.dispatch.ptr()
// OGCG: %[[GEP:.*]] = getelementptr i8, ptr addrspace(4) %[[DP]], i32 12
// OGCG: load i32, ptr addrspace(4) %[[GEP]], align 4, !range ![[RANGE:[0-9]+]], !invariant.load ![[INV:[0-9]+]]

unsigned test_grid_size_y(void) {
  return __builtin_amdgcn_grid_size_y();
}

// CIR-LABEL: @test_grid_size_y
// CIR: cir.const #cir.int<16>
// LLVM-LABEL: @test_grid_size_y
// LLVM: getelementptr i8, ptr addrspace(4) %{{.*}}, i64 16
// OGCG-LABEL: @test_grid_size_y
// OGCG: getelementptr i8, ptr addrspace(4) %{{.*}}, i32 16

unsigned test_grid_size_z(void) {
  return __builtin_amdgcn_grid_size_z();
}

// CIR-LABEL: @test_grid_size_z
// CIR: cir.const #cir.int<20>
// LLVM-LABEL: @test_grid_size_z
// LLVM: getelementptr i8, ptr addrspace(4) %{{.*}}, i64 20
// OGCG-LABEL: @test_grid_size_z
// OGCG: getelementptr i8, ptr addrspace(4) %{{.*}}, i32 20

// LLVM: ![[RANGE]] = !{i32 1, i32 0}
// OGCG: ![[RANGE]] = !{i32 1, i32 0}
