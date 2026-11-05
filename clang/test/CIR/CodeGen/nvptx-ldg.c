// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +ptx70 -ffreestanding -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +ptx70 -ffreestanding -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -target-feature +ptx70 -ffreestanding -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// The scalar __nvvm_ldg_* builtins are read-only global loads. Classic ref:
// clang/lib/CodeGen/TargetBuiltins/NVPTX.cpp MakeLdg -> an addrspacecast of the
// generic pointer to the NVPTX global address space (addrspace(1)) followed by
// an aligned load carrying !invariant.load.

int test_ldg_i(const int *p) {
  return __nvvm_ldg_i(p);
}

// CIR-LABEL: @test_ldg_i
// CIR: %[[AS:.*]] = cir.cast address_space {{.*}} : !cir.ptr<!s32i> -> !cir.ptr<!s32i, target_address_space(1)>
// CIR: cir.load align(4) %[[AS]] : !cir.ptr<!s32i, target_address_space(1)>, !s32i {cir.invariant_load}
// LLVM-LABEL: @test_ldg_i
// LLVM: %[[P:.*]] = addrspacecast ptr %{{.*}} to ptr addrspace(1)
// LLVM: load i32, ptr addrspace(1) %[[P]], align 4, !invariant.load
// OGCG-LABEL: @test_ldg_i
// OGCG: %[[P:.*]] = addrspacecast ptr %{{.*}} to ptr addrspace(1)
// OGCG: load i32, ptr addrspace(1) %[[P]], align 4, !invariant.load

long test_ldg_l(const long *p) {
  return __nvvm_ldg_l(p);
}

// CIR-LABEL: @test_ldg_l
// CIR: cir.load align(8) {{.*}} : !cir.ptr<!s64i, target_address_space(1)>, !s64i {cir.invariant_load}
// LLVM-LABEL: @test_ldg_l
// LLVM: load i64, ptr addrspace(1) %{{.*}}, align 8, !invariant.load
// OGCG-LABEL: @test_ldg_l
// OGCG: load i64, ptr addrspace(1) %{{.*}}, align 8, !invariant.load

float test_ldg_f(const float *p) {
  return __nvvm_ldg_f(p);
}

// CIR-LABEL: @test_ldg_f
// CIR: cir.load align(4) {{.*}} : !cir.ptr<!cir.float, target_address_space(1)>, !cir.float {cir.invariant_load}
// LLVM-LABEL: @test_ldg_f
// LLVM: load float, ptr addrspace(1) %{{.*}}, align 4, !invariant.load
// OGCG-LABEL: @test_ldg_f
// OGCG: load float, ptr addrspace(1) %{{.*}}, align 4, !invariant.load

double test_ldg_d(const double *p) {
  return __nvvm_ldg_d(p);
}

// CIR-LABEL: @test_ldg_d
// CIR: cir.load align(8) {{.*}} : !cir.ptr<!cir.double, target_address_space(1)>, !cir.double {cir.invariant_load}
// LLVM-LABEL: @test_ldg_d
// LLVM: load double, ptr addrspace(1) %{{.*}}, align 8, !invariant.load
// OGCG-LABEL: @test_ldg_d
// OGCG: load double, ptr addrspace(1) %{{.*}}, align 8, !invariant.load

unsigned char test_ldg_uc(const unsigned char *p) {
  return __nvvm_ldg_uc(p);
}

// CIR-LABEL: @test_ldg_uc
// CIR: cir.load align(1) {{.*}} : !cir.ptr<!u8i, target_address_space(1)>, !u8i {cir.invariant_load}
// LLVM-LABEL: @test_ldg_uc
// LLVM: load i8, ptr addrspace(1) %{{.*}}, align 1, !invariant.load
// OGCG-LABEL: @test_ldg_uc
// OGCG: load i8, ptr addrspace(1) %{{.*}}, align 1, !invariant.load

// The vector __nvvm_ldg_*{2,4} variants go through the same MakeLdg path: the
// pointee is a vector type and the alignment is the natural vector alignment
// (n * alignof(elt) rounded to a power of two), computed by
// getNaturalPointeeTypeAlignment with no special casing.

typedef int int2 __attribute__((ext_vector_type(2)));
typedef int int4 __attribute__((ext_vector_type(4)));
typedef double double2 __attribute__((ext_vector_type(2)));
typedef char char4 __attribute__((ext_vector_type(4)));

int2 test_ldg_i2(const int2 *p) {
  return __nvvm_ldg_i2(p);
}

// CIR-LABEL: @test_ldg_i2
// CIR: cir.load align(8) {{.*}} : !cir.ptr<!cir.vector<2 x !s32i>, target_address_space(1)>, !cir.vector<2 x !s32i> {cir.invariant_load}
// LLVM-LABEL: @test_ldg_i2
// LLVM: load <2 x i32>, ptr addrspace(1) %{{.*}}, align 8, !invariant.load
// OGCG-LABEL: @test_ldg_i2
// OGCG: load <2 x i32>, ptr addrspace(1) %{{.*}}, align 8, !invariant.load

int4 test_ldg_i4(const int4 *p) {
  return __nvvm_ldg_i4(p);
}

// CIR-LABEL: @test_ldg_i4
// CIR: cir.load align(16) {{.*}} : !cir.ptr<!cir.vector<4 x !s32i>, target_address_space(1)>, !cir.vector<4 x !s32i> {cir.invariant_load}
// LLVM-LABEL: @test_ldg_i4
// LLVM: load <4 x i32>, ptr addrspace(1) %{{.*}}, align 16, !invariant.load
// OGCG-LABEL: @test_ldg_i4
// OGCG: load <4 x i32>, ptr addrspace(1) %{{.*}}, align 16, !invariant.load

double2 test_ldg_d2(const double2 *p) {
  return __nvvm_ldg_d2(p);
}

// CIR-LABEL: @test_ldg_d2
// CIR: cir.load align(16) {{.*}} : !cir.ptr<!cir.vector<2 x !cir.double>, target_address_space(1)>, !cir.vector<2 x !cir.double> {cir.invariant_load}
// LLVM-LABEL: @test_ldg_d2
// LLVM: load <2 x double>, ptr addrspace(1) %{{.*}}, align 16, !invariant.load
// OGCG-LABEL: @test_ldg_d2
// OGCG: load <2 x double>, ptr addrspace(1) %{{.*}}, align 16, !invariant.load

char4 test_ldg_c4(const char4 *p) {
  return __nvvm_ldg_c4(p);
}

// CIR-LABEL: @test_ldg_c4
// CIR: cir.load align(4) {{.*}} : !cir.ptr<!cir.vector<4 x !s8i>, target_address_space(1)>, !cir.vector<4 x !s8i> {cir.invariant_load}
// LLVM-LABEL: @test_ldg_c4
// LLVM: load <4 x i8>, ptr addrspace(1) %{{.*}}, align 4, !invariant.load
// OGCG-LABEL: @test_ldg_c4
// OGCG: load <4 x i8>, ptr addrspace(1) %{{.*}}, align 4, !invariant.load
