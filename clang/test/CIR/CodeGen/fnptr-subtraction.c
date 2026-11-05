// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll --check-prefix=OGCG %s

// Function pointer subtraction is a GCC extension where sizeof(function) == 1.

void f0(void (*fp)(void), void (*fp2)(void)) {
  int x = fp - fp2;
}

// CIR-LABEL: @f0
// CIR: %[[LHS:.*]] = cir.load {{.*}} : !cir.ptr<!cir.ptr<!cir.func<()>>>, !cir.ptr<!cir.func<()>>
// CIR: %[[RHS:.*]] = cir.load {{.*}} : !cir.ptr<!cir.ptr<!cir.func<()>>>, !cir.ptr<!cir.func<()>>
// CIR: %[[DIFF:.*]] = cir.ptr_diff %[[LHS]], %[[RHS]] : !cir.ptr<!cir.func<()>> -> !s64i
// CIR: %[[TRUNC:.*]] = cir.cast integral %[[DIFF]] : !s64i -> !s32i

// LLVM-LABEL: @f0
// LLVM: %[[LHS:.*]] = ptrtoint ptr %{{.*}} to i64
// LLVM: %[[RHS:.*]] = ptrtoint ptr %{{.*}} to i64
// LLVM: %[[DIFF:.*]] = sub i64 %[[LHS]], %[[RHS]]
// LLVM: trunc i64 %[[DIFF]] to i32

// OGCG-LABEL: @f0
// OGCG: %[[LHS:.*]] = ptrtoint ptr %{{.*}} to i64
// OGCG: %[[RHS:.*]] = ptrtoint ptr %{{.*}} to i64
// OGCG: %[[DIFF:.*]] = sub i64 %[[LHS]], %[[RHS]]
// OGCG: trunc i64 %[[DIFF]] to i32
