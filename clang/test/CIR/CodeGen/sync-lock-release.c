// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG

int intval;
void test_lock_release_int(void) {
  __sync_lock_release(&intval);
}
// CIR-LABEL: @test_lock_release_int
// CIR: %[[ZERO:.*]] = cir.const #cir.int<0> : !s32i
// CIR: cir.store align(4) atomic(release) %[[ZERO]], %{{.*}} : !s32i, !cir.ptr<!s32i>

// LLVM-LABEL: @test_lock_release_int
// LLVM: store atomic i32 0, ptr @intval release, align 4

// OGCG-LABEL: @test_lock_release_int
// OGCG: store atomic i32 0, ptr @intval release, align 4

int *ptrval;
void test_lock_release_ptr(void) {
  __sync_lock_release(&ptrval);
}
// CIR-LABEL: @test_lock_release_ptr
// CIR: %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!s32i>
// CIR: cir.store align(8) atomic(release) %[[NULL]], %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>

// Note: CIR produces `store atomic ptr null` while OGCG produces
// `store atomic i64 0`. Both are semantically correct — CIR preserves
// pointer typing through lowering while OGCG always uses integer types.
// LLVM-LABEL: @test_lock_release_ptr
// LLVM: store atomic ptr null, ptr @ptrval release, align 8

// OGCG-LABEL: @test_lock_release_ptr
// OGCG: store atomic i64 0, ptr @ptrval release, align 8
