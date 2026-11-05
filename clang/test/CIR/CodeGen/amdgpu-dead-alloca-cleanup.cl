// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -fclangir -emit-cir %s \
// RUN:   -o %t.cir -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -fclangir -emit-llvm %s \
// RUN:   -O0 -disable-llvm-passes -o %t.ll -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -x cl -triple amdgcn-amd-amdhsa -emit-llvm %s \
// RUN:   -O0 -disable-llvm-passes -o %t.ogcg.ll -cl-std=CL2.0
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that parameter allocas on AMDGCN carry the private address space
// (offload_private) and that call convention lowering properly cleans up
// dead allocas. Previously CIRGen would insert unnecessary addrspacecasts
// from the alloca address space to flat; now the allocas directly carry
// the language address space.

typedef struct { int x; float y; } SmallStruct;
typedef struct { int data[4]; } MediumStruct;

// --- Direct coercion: struct coerced to array type ---
void takes_small(SmallStruct s) {}

// The parameter alloca should carry lang_address_space(offload_private).
// CIR-LABEL: @takes_small
// CIR: cir.alloca {{.*}}lang_address_space(offload_private)
// CIR: cir.return

// --- IndirectAliased: kernel wrapper with byref pointer ---
kernel void kern_small(SmallStruct s) {}
kernel void kern_medium(MediumStruct s) {}

// Kernel wrappers should have allocas with lang_address_space(offload_private).
// CIR-LABEL: @kern_small(
// CIR: cir.alloca {{.*}}lang_address_space(offload_private)
// CIR: cir.return

// CIR-LABEL: @kern_medium(
// CIR: cir.alloca {{.*}}lang_address_space(offload_private)
// CIR: cir.return

// Verify LLVM output: kernel wrappers have alloca + memcpy from byref arg.
// LLVM-LABEL: define {{.*}}amdgpu_kernel void @kern_small(
// LLVM: alloca %struct.SmallStruct
// LLVM: call void @llvm.memcpy

// LLVM-LABEL: define {{.*}}amdgpu_kernel void @kern_medium(
// LLVM: alloca %struct.MediumStruct
// LLVM: call void @llvm.memcpy

// OGCG-LABEL: define {{.*}}amdgpu_kernel void @kern_small(
// OGCG: alloca %struct.SmallStruct
// OGCG: call void @llvm.memcpy

// OGCG-LABEL: define {{.*}}amdgpu_kernel void @kern_medium(
// OGCG: alloca %struct.MediumStruct
// OGCG: call void @llvm.memcpy
