// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN: -emit-llvm %s -o %t.noccl.ll
// RUN: FileCheck --input-file=%t.noccl.ll %s --check-prefix=NOCCL
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Forwarding an indirect (sret) record result straight out of a return --
// `return callee();` -- has to write the caller's own sret slot.
//
// The epilog used to reach the sret argument only by rewriting the uses of the
// __retval alloca. mem2reg promotes that slot, leaving `cir.return %call` with
// the value in an SSA register and no alloca to redirect, and the operand was
// then dropped -- so the caller's sret memory was left uninitialized. Both
// flags matter: mem2reg is a driver default, and -fno-clangir-call-conv-lowering
// defers call-conv lowering until after it (with call-conv lowering in its
// default position the slot is still in memory when the epilog runs).

struct Big {
  long a, b, c, d;
};

Big declaredOnly();

// 1. Callee declared only: every cross-TU call looks like this.
Big fwdDeclOnly() { return declaredOnly(); }

// CIR-LABEL: define{{.*}} void @_Z11fwdDeclOnlyv(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]])
// CIR-NOT:     alloca
// CIR:         call void @_Z12declaredOnlyv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// CIR-NEXT:    ret void

// NOCCL-LABEL: define{{.*}} void @_Z11fwdDeclOnlyv(
// NOCCL-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]])
// NOCCL-NOT:     alloca
// NOCCL:         call void @_Z12declaredOnlyv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// NOCCL-NEXT:    ret void

// OGCG-LABEL: define{{.*}} void @_Z11fwdDeclOnlyv(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]])
// OGCG-NOT:     alloca
// OGCG:         call void @_Z12declaredOnlyv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// OGCG-NEXT:    ret void

// 2. Callee defined after the caller: the same ordering hazard inside one TU.
Big definedLater();
Big fwdDefinedLater() { return definedLater(); }
Big definedLater() { return Big{1, 2, 3, 4}; }

// CIR-LABEL: define{{.*}} void @_Z15fwdDefinedLaterv(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]])
// CIR-NOT:     alloca
// CIR:         call void @_Z12definedLaterv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// CIR-NEXT:    ret void

// NOCCL-LABEL: define{{.*}} void @_Z15fwdDefinedLaterv(
// NOCCL-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]])
// NOCCL-NOT:     alloca
// NOCCL:         call void @_Z12definedLaterv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// NOCCL-NEXT:    ret void

// OGCG-LABEL: define{{.*}} void @_Z15fwdDefinedLaterv(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]])
// OGCG-NOT:     alloca
// OGCG:         call void @_Z12definedLaterv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// OGCG-NEXT:    ret void

// 3. Control: with more than one return the slot stays in memory, so mem2reg
//    does not promote it and this shape was already correct. It is here to pin
//    that the fix does not regress the in-memory path -- every return edge must
//    still write the sret slot, and exactly once.
Big fwdConditional(bool c) {
  if (c)
    return declaredOnly();
  return Big{0, 0, 0, 0};
}

// CIR-LABEL: define{{.*}} void @_Z14fwdConditionalb(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// CIR:         call void @_Z12declaredOnlyv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// CIR:         call void @llvm.memset.p0.i64(ptr align 8 [[RET]], i8 0, i64 32, i1 false)

// NOCCL-LABEL: define{{.*}} void @_Z14fwdConditionalb(
// NOCCL-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// NOCCL:         call void @_Z12declaredOnlyv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// NOCCL:         call void @llvm.memset.p0.i64(ptr align 8 [[RET]], i8 0, i64 32, i1 false)

// OGCG-LABEL: define{{.*}} void @_Z14fwdConditionalb(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// OGCG:         call void @_Z12declaredOnlyv(ptr{{.*}} sret(%struct.Big) align 8 [[RET]])
// OGCG:         call void @llvm.memset.p0.i64(ptr align 8 [[RET]], i8 0, i64 32, i1 false)
