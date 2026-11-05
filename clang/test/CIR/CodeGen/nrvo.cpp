// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-elide-constructors -fclangir -emit-mlir=cir-abi %s -o %t-noelide.cir
// RUN: FileCheck --input-file=%t-noelide.cir %s --check-prefix=CIR-NOELIDE
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// There are no LLVM and OGCG tests with -fno-elide-constructors because the
// lowering isn't of interest for this test. We just need to see that the
// copy constructor is elided without -fno-elide-constructors but not with it.

struct S {
  S();
  int a;
  int b;
};

struct S f1() {
  S s;
  return s;
}

// CIR:      cir.func{{.*}} @_Z2f1v() -> !u64i
// CIR-NEXT:   %[[RETVAL:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["__retval", init]
// CIR-NEXT:   cir.call @_ZN1SC1Ev(%[[RETVAL]]) : (!cir.ptr<!rec_S>) -> ()
// CIR-NEXT:   %[[RETCAST:.*]] = cir.cast bitcast %[[RETVAL]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR-NEXT:   %[[RETCOERCED:.*]] = cir.load align(4) %[[RETCAST]] : !cir.ptr<!u64i>, !u64i
// CIR-NEXT:   cir.return %[[RETCOERCED]]

// CIR-NOELIDE:      cir.func{{.*}} @_Z2f1v() -> !u64i
// CIR-NOELIDE-NEXT:   %[[RETVAL:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["__retval"]
// CIR-NOELIDE-NEXT:   %[[S:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s", init]
// CIR-NOELIDE-NEXT:   cir.call @_ZN1SC1Ev(%[[S]]) : (!cir.ptr<!rec_S>) -> ()
// CIR-NOELIDE-NEXT:   cir.copy %[[S]] to %[[RETVAL]] {{.*}}: !cir.ptr<!rec_S>
// CIR-NOELIDE-NEXT:   %[[RETCAST:.*]] = cir.cast bitcast %[[RETVAL]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR-NOELIDE-NEXT:   %[[RETCOERCED:.*]] = cir.load align(4) %[[RETCAST]] : !cir.ptr<!u64i>, !u64i
// CIR-NOELIDE-NEXT:   cir.return %[[RETCOERCED]]

// FIXME: Update this when calling convention lowering is implemented.
// LLVM:      define{{.*}} i64 @_Z2f1v()
// LLVM-NEXT:   %[[RETVAL:.*]] = alloca %struct.S
// LLVM-NEXT:   call void @_ZN1SC1Ev(ptr {{[^%]*}}%[[RETVAL]])
// LLVM-NEXT:   %[[RETCOERCED:.*]] = load i64, ptr %[[RETVAL]], align 4
// LLVM-NEXT:   ret i64 %[[RETCOERCED]]

// OGCG:      define{{.*}} i64 @_Z2f1v()
// OGCG-NEXT: entry:
// OGCG-NEXT:   %[[RETVAL:.*]] = alloca %struct.S
// OGCG-NEXT:   call void @_ZN1SC1Ev(ptr {{.*}} %[[RETVAL]])
// OGCG-NEXT:   %[[RET:.*]] = load i64, ptr %[[RETVAL]]
// OGCG-NEXT:   ret i64 %[[RET]]

struct NonTrivial {
  ~NonTrivial();
};

void maybeThrow();

NonTrivial test_nrvo() {
  NonTrivial result;
  maybeThrow();
  return result;
}

// CIR: cir.func {{.*}} @_Z9test_nrvov(%arg0: !cir.ptr<!rec_NonTrivial> {cir.align = 1 : i64, cir.dead_on_unwind, cir.noalias, cir.sret = !rec_NonTrivial, cir.writable}
// CIR:   %[[NRVO_FLAG:.*]] = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["nrvo"]
// CIR:   %[[FALSE:.*]] = cir.const #false
// CIR:   cir.store{{.*}} %[[FALSE]], %[[NRVO_FLAG]]
// CIR:   cir.call @_Z10maybeThrowv() : () -> ()
// CIR:   %[[TRUE:.*]] = cir.const #true
// CIR:   cir.store{{.*}} %[[TRUE]], %[[NRVO_FLAG]]
// CIR:   %[[NRVO_FLAG_VAL:.*]] = cir.load{{.*}} %[[NRVO_FLAG]]
// CIR:   %[[NOT_NRVO_VAL:.*]] = cir.unary(not, %[[NRVO_FLAG_VAL]])
// CIR:   cir.if %[[NOT_NRVO_VAL]] {
// CIR:     cir.call @_ZN10NonTrivialD1Ev(%arg0)
// CIR:   }
// CIR:   cir.return

// LLVM: define {{.*}} void @_Z9test_nrvov(ptr dead_on_unwind noalias writable sret(%struct.NonTrivial) align 1 %[[RESULT:.*]])
// LLVM:   %[[NRVO_FLAG:.*]] = alloca i8
// LLVM:   store i8 0, ptr %[[NRVO_FLAG]]
// LLVM:   call void @_Z10maybeThrowv()
// LLVM:   store i8 1, ptr %[[NRVO_FLAG]]
// LLVM:   %[[NRVO_VAL:.*]] = load i8, ptr %[[NRVO_FLAG]]
// LLVM:   %[[NRVO_TRUNC:.*]] = trunc i8 %[[NRVO_VAL]] to i1
// LLVM:   %[[NOT_NRVO:.*]] = xor i1 %[[NRVO_TRUNC]], true
// LLVM:   br i1 %[[NOT_NRVO]]
// LLVM:   call void @_ZN10NonTrivialD1Ev(ptr {{[^%]*}}%[[RESULT]])
// LLVM:   ret void

// OGCG: define {{.*}} void @_Z9test_nrvov(ptr {{.*}} sret(%struct.NonTrivial) {{.*}} %[[RESULT:.*]])
// OGCG:   %[[RESULT_ADDR:.*]] = alloca ptr
// OGCG:   %[[NRVO_FLAG:.*]] = alloca i1, align 1
// OGCG:   store ptr %[[RESULT]], ptr %[[RESULT_ADDR]]
// OGCG:   store i1 false, ptr %[[NRVO_FLAG]]
// OGCG:   call void @_Z10maybeThrowv()
// OGCG:   store i1 true, ptr %[[NRVO_FLAG]]
// OGCG:   %[[NRVO_VAL:.*]] = load i1, ptr %[[NRVO_FLAG]]
// OGCG:   br i1 %[[NRVO_VAL]], label %[[SKIPDTOR:.*]], label %[[NRVO_UNUSED:.*]]
// OGCG: [[NRVO_UNUSED]]:
// OGCG:   call void @_ZN10NonTrivialD1Ev(ptr {{.*}} %[[RESULT]])
// OGCG:   br label %[[SKIPDTOR]]
// OGCG: [[SKIPDTOR]]:
// OGCG:   ret void
