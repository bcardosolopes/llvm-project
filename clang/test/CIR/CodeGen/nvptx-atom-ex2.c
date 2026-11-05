// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +ptx70 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +ptx70 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +ptx70 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// These NVVM builtins need special CIR handling. Classic ref:
// clang/lib/CodeGen/TargetBuiltins/NVPTX.cpp.
//   __nvvm_atom_<op>_gen_* -> a generic-address-space atomicrmw/cmpxchg (seq_cst)
//   __nvvm_ex2_approx_f     -> llvm.nvvm.ex2.approx.f32

int test_atom_add(int *p, int v) {
  return __nvvm_atom_add_gen_i(p, v);
}

// CIR-LABEL: @test_atom_add
// CIR: cir.atomic.fetch add seq_cst fetch_first {{.*}} align(4) : (!cir.ptr<!s32i>, !s32i) -> !s32i
// LLVM-LABEL: @test_atom_add
// LLVM: atomicrmw add ptr %{{.*}}, i32 %{{.*}} seq_cst, align 4
// OGCG-LABEL: @test_atom_add
// OGCG: atomicrmw add ptr %{{.*}}, i32 %{{.*}} seq_cst, align 4

float test_atom_add_f(float *p, float v) {
  return __nvvm_atom_add_gen_f(p, v);
}

// CIR-LABEL: @test_atom_add_f
// CIR: cir.atomic.fetch add seq_cst fetch_first {{.*}} align(4) : (!cir.ptr<!cir.float>, !cir.float) -> !cir.float
// LLVM-LABEL: @test_atom_add_f
// LLVM: atomicrmw fadd ptr %{{.*}}, float %{{.*}} seq_cst, align 4
// OGCG-LABEL: @test_atom_add_f
// OGCG: atomicrmw fadd ptr %{{.*}}, float %{{.*}} seq_cst, align 4

int test_atom_and(int *p, int v) {
  return __nvvm_atom_and_gen_i(p, v);
}

// CIR-LABEL: @test_atom_and
// CIR: cir.atomic.fetch and seq_cst fetch_first {{.*}} align(4) : (!cir.ptr<!s32i>, !s32i) -> !s32i
// LLVM-LABEL: @test_atom_and
// LLVM: atomicrmw and ptr %{{.*}}, i32 %{{.*}} seq_cst, align 4
// OGCG-LABEL: @test_atom_and
// OGCG: atomicrmw and ptr %{{.*}}, i32 %{{.*}} seq_cst, align 4

int test_atom_xchg(int *p, int v) {
  return __nvvm_atom_xchg_gen_i(p, v);
}

// CIR-LABEL: @test_atom_xchg
// CIR: cir.atomic.xchg seq_cst {{.*}} align(4) : (!cir.ptr<!s32i>, !s32i) -> !s32i
// LLVM-LABEL: @test_atom_xchg
// LLVM: atomicrmw xchg ptr %{{.*}}, i32 %{{.*}} seq_cst, align 4
// OGCG-LABEL: @test_atom_xchg
// OGCG: atomicrmw xchg ptr %{{.*}}, i32 %{{.*}} seq_cst, align 4

int test_atom_max(int *p, int v) {
  return __nvvm_atom_max_gen_i(p, v);
}

// CIR-LABEL: @test_atom_max
// CIR: cir.atomic.fetch max seq_cst fetch_first {{.*}} align(4) : (!cir.ptr<!s32i>, !s32i) -> !s32i
// LLVM-LABEL: @test_atom_max
// LLVM: atomicrmw max ptr %{{.*}}, i32 %{{.*}} seq_cst, align 4
// OGCG-LABEL: @test_atom_max
// OGCG: atomicrmw max ptr %{{.*}}, i32 %{{.*}} seq_cst, align 4

int test_atom_cas(int *p, int a, int b) {
  return __nvvm_atom_cas_gen_i(p, a, b);
}

// CIR-LABEL: @test_atom_cas
// CIR: cir.atomic.cmpxchg success(seq_cst) failure(seq_cst) {{.*}} align(4) : (!cir.ptr<!s32i>, !s32i, !s32i) -> (!s32i, !cir.bool)
// LLVM-LABEL: @test_atom_cas
// LLVM: %[[CX:.*]] = cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
// LLVM: extractvalue { i32, i1 } %[[CX]], 0
// OGCG-LABEL: @test_atom_cas
// OGCG: %[[CX:.*]] = cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
// OGCG: extractvalue { i32, i1 } %[[CX]], 0

float test_ex2(float a) {
  return __nvvm_ex2_approx_f(a);
}

// CIR-LABEL: @test_ex2
// CIR: cir.call_llvm_intrinsic "nvvm.ex2.approx" {{.*}} : (!cir.float) -> !cir.float
// LLVM-LABEL: @test_ex2
// LLVM: call float @llvm.nvvm.ex2.approx.f32(float %{{.*}})
// OGCG-LABEL: @test_ex2
// OGCG: call float @llvm.nvvm.ex2.approx.f32(float %{{.*}})
