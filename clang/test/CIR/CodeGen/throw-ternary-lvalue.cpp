// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fexceptions -fcxx-exceptions -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fexceptions -fcxx-exceptions -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s

extern int val;

// Throw in false branch - lvalue ternary (was SSA dominance violation)
int &test_throw_false(bool cond) {
    return cond ? val : ((throw "foo"));
}

// CIR-LABEL: cir.func{{.*}} @_Z16test_throw_falseb(
// CIR: %[[COND:.*]] = cir.load{{.*}} !cir.ptr<!cir.bool>, !cir.bool
// CIR: %[[RESULT:.*]] = cir.ternary(%[[COND]], true {
// CIR:   %[[VAL:.*]] = cir.get_global @val : !cir.ptr<!s32i>
// CIR:   cir.yield %[[VAL]] : !cir.ptr<!s32i>
// CIR: }, false {
// CIR:   cir.throw
// CIR:   cir.unreachable
// CIR:   %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!s32i>
// CIR:   cir.yield %[[NULL]] : !cir.ptr<!s32i>
// CIR: }) : (!cir.bool) -> !cir.ptr<!s32i>
// CIR: cir.return %[[RESULT]] : !cir.ptr<!s32i>

// LLVM-LABEL: define{{.*}} ptr @_Z16test_throw_falseb(
// LLVM: br i1 %{{.*}}, label %[[TRUE_BB:.*]], label %[[FALSE_BB:.*]]
// LLVM: [[TRUE_BB]]:
// LLVM:   br label %[[PHI_BB:.*]]
// LLVM: [[FALSE_BB]]:
// LLVM:   call void @__cxa_throw
// LLVM:   unreachable
// LLVM: [[PHI_BB]]:
// LLVM:   %[[PHI:.*]] = phi ptr [ @val, %[[TRUE_BB]] ]

// Throw in true branch - lvalue ternary (was SSA dominance violation)
int &test_throw_true(bool cond) {
    return cond ? ((throw "foo")) : val;
}

// CIR-LABEL: cir.func{{.*}} @_Z15test_throw_trueb(
// CIR: %[[COND:.*]] = cir.load{{.*}} !cir.ptr<!cir.bool>, !cir.bool
// CIR: %[[RESULT:.*]] = cir.ternary(%[[COND]], true {
// CIR:   cir.throw
// CIR:   cir.unreachable
// CIR:   %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!s32i>
// CIR:   cir.yield %[[NULL]] : !cir.ptr<!s32i>
// CIR: }, false {
// CIR:   %[[VAL:.*]] = cir.get_global @val : !cir.ptr<!s32i>
// CIR:   cir.yield %[[VAL]] : !cir.ptr<!s32i>
// CIR: }) : (!cir.bool) -> !cir.ptr<!s32i>
// CIR: cir.return %[[RESULT]] : !cir.ptr<!s32i>

// LLVM-LABEL: define{{.*}} ptr @_Z15test_throw_trueb(
// LLVM: br i1 %{{.*}}, label %[[TRUE_BB:.*]], label %[[FALSE_BB:.*]]
// LLVM: [[TRUE_BB]]:
// LLVM:   call void @__cxa_throw
// LLVM:   unreachable
// LLVM: [[FALSE_BB]]:
// LLVM:   br label %[[PHI_BB:.*]]
// LLVM: [[PHI_BB]]:
// LLVM:   %[[PHI:.*]] = phi ptr [ @val, %[[FALSE_BB]] ]
