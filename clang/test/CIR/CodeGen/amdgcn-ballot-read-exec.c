// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-feature +wavefrontsize32 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-feature +wavefrontsize32 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-feature +wavefrontsize32 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// The AMDGPU ballot / read_exec builtins need special CIR handling: they lower
// to the amdgcn.ballot intrinsic (overloaded on the result / register type).
// read_exec* pass a constant i1 true; read_exec_hi extracts the high 32 bits.
// Classic ref: clang/lib/CodeGen/TargetBuiltins/AMDGPU.cpp.

unsigned int test_ballot_w32(int a) {
  return __builtin_amdgcn_ballot_w32(a == 0);
}

// CIR-LABEL: @test_ballot_w32
// CIR: cir.call_llvm_intrinsic "amdgcn.ballot" {{.*}} : (!cir.bool) -> !u32i
// LLVM-LABEL: @test_ballot_w32
// LLVM: call i32 @llvm.amdgcn.ballot.i32(i1 %{{.*}})
// OGCG-LABEL: @test_ballot_w32
// OGCG: call i32 @llvm.amdgcn.ballot.i32(i1 %{{.*}})

unsigned long test_ballot_w64(int a) {
  return __builtin_amdgcn_ballot_w64(a == 0);
}

// CIR-LABEL: @test_ballot_w64
// CIR: cir.call_llvm_intrinsic "amdgcn.ballot" {{.*}} : (!cir.bool) -> !u64i
// LLVM-LABEL: @test_ballot_w64
// LLVM: call i64 @llvm.amdgcn.ballot.i64(i1 %{{.*}})
// OGCG-LABEL: @test_ballot_w64
// OGCG: call i64 @llvm.amdgcn.ballot.i64(i1 %{{.*}})

unsigned long test_read_exec(void) {
  return __builtin_amdgcn_read_exec();
}

// CIR-LABEL: @test_read_exec
// CIR: %[[TRUE:.*]] = cir.const #true
// CIR: cir.call_llvm_intrinsic "amdgcn.ballot" %[[TRUE]] : (!cir.bool) -> !u64i
// LLVM-LABEL: @test_read_exec
// LLVM: call i64 @llvm.amdgcn.ballot.i64(i1 true)
// OGCG-LABEL: @test_read_exec
// OGCG: call i64 @llvm.amdgcn.ballot.i64(i1 true)

unsigned int test_read_exec_lo(void) {
  return __builtin_amdgcn_read_exec_lo();
}

// CIR-LABEL: @test_read_exec_lo
// CIR: cir.call_llvm_intrinsic "amdgcn.ballot" {{.*}} : (!cir.bool) -> !u32i
// LLVM-LABEL: @test_read_exec_lo
// LLVM: call i32 @llvm.amdgcn.ballot.i32(i1 true)
// OGCG-LABEL: @test_read_exec_lo
// OGCG: call i32 @llvm.amdgcn.ballot.i32(i1 true)

unsigned int test_read_exec_hi(void) {
  return __builtin_amdgcn_read_exec_hi();
}

// CIR-LABEL: @test_read_exec_hi
// CIR: %[[BALLOT:.*]] = cir.call_llvm_intrinsic "amdgcn.ballot" {{.*}} : (!cir.bool) -> !u64i
// CIR: %[[SHIFT:.*]] = cir.shift(right, %[[BALLOT]] : !u64i, {{.*}} : !u64i) -> !u64i
// CIR: cir.cast integral %[[SHIFT]] : !u64i -> !u32i
// LLVM-LABEL: @test_read_exec_hi
// LLVM: %[[B:.*]] = call i64 @llvm.amdgcn.ballot.i64(i1 true)
// LLVM: %[[S:.*]] = lshr i64 %[[B]], 32
// LLVM: trunc i64 %[[S]] to i32
// OGCG-LABEL: @test_read_exec_hi
// OGCG: %[[B:.*]] = call i64 @llvm.amdgcn.ballot.i64(i1 true)
// OGCG: %[[S:.*]] = lshr i64 %[[B]], 32
// OGCG: trunc i64 %[[S]] to i32
