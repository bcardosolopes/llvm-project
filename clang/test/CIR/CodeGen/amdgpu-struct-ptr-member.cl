// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that structs containing pointers with non-default address spaces
// (e.g., private AS on AMDGCN which uses 32-bit pointers) get correct
// record layout. Previously, all pointer members used the default AS
// pointer width (64-bit), causing assertion failures in CIRRecordLowering.

struct S {
  int x;
  int y;
  int *z;  // Private AS on AMDGCN: 32-bit pointer
};

// CIR: !cir.record<struct "S" {!s32i, !s32i, !cir.ptr<!s32i, lang_address_space(offload_private)>}>

// The struct is small enough to be passed as 3 separate arguments on AMDGCN.
// LLVM: define dso_local void @test(i32 %{{.*}}, i32 %{{.*}}, ptr addrspace(5) {{.*}})
// OGCG: define dso_local void @test(i32 %{{.*}}, i32 %{{.*}}, ptr addrspace(5) {{.*}})
void test(struct S s) {}
