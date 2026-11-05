// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// We declare anonymous record types to represent lambdas. Rather than trying to
// to match the declarations, we establish variables for these when they are used.

// Test void lambda with static invoker to verify void forwarding branch.
int g_side_effect = 0;
void g_void() {
  auto* fn = +[](int x) -> void { g_side_effect = x; };
  fn(42);
}

// Void lambda operator()
// CIR: cir.func {{.*}}lambda {{.*}} @_ZZ6g_voidvENK3$_0clEi(
// CIR:   cir.return

// Void lambda invoker — exercises emitForwardingCallToLambda void path
// CIR: cir.func {{.*}} @_ZZ6g_voidvEN3$_08__invokeEi(
// CIR:   cir.call @_ZZ6g_voidvENK3$_0clEi(
// CIR:   cir.return

// LLVM: define internal void @"_ZZ6g_voidvEN3$_08__invokeEi"(i32 noundef
// LLVM:   call void @"_ZZ6g_voidvENK3$_0clEi"(
// LLVM:   ret void

int g3() {
  auto* fn = +[](int const& i) -> int { return i; };
  auto task = fn(3);
  return task;
}

// The order of these functions is different in OGCG.

// OGCG: define dso_local noundef i32 @_Z2g3v()
// OGCG:   %[[FN_PTR:.*]] = alloca ptr
// OGCG:   %[[REF_TMP:.*]] = alloca %[[REC_LAM_G3:.*]]
// OGCG:   %[[TASK:.*]] = alloca i32
// OGCG:   %[[REF_TMP1:.*]] = alloca i32
// OGCG:   %[[CALL:.*]] = call {{.*}} ptr @"_ZZ2g3vENK3$_0cvPFiRKiEEv"(ptr {{.*}} %[[REF_TMP]])
// OGCG:   store ptr %[[CALL]], ptr %[[FN_PTR]]
// OGCG:   %[[FN:.*]] = load ptr, ptr %[[FN_PTR]]
// OGCG:   store i32 3, ptr %[[REF_TMP1]]
// OGCG:   %[[CALL2:.*]] = call {{.*}} i32 %[[FN]](ptr {{.*}} %[[REF_TMP1]])
// OGCG:   store i32 %[[CALL2]], ptr %[[TASK]]
// OGCG:   %[[RESULT:.*]] = load i32, ptr %[[TASK]]
// OGCG:   ret i32 %[[RESULT]]

// OGCG: define internal noundef ptr @"_ZZ2g3vENK3$_0cvPFiRKiEEv"(ptr {{.*}} %[[THIS_ARG:.*]])
// OGCG:   %[[THIS_ADDR:.*]] = alloca ptr
// OGCG:   store ptr %[[THIS_ARG]], ptr %[[THIS_ADDR]]
// OGCG:   %[[THIS:.*]] = load ptr, ptr %[[THIS_ADDR]]
// OGCG:   ret ptr @"_ZZ2g3vEN3$_08__invokeERKi"

// lambda operator()
// CIR: cir.func {{.*}}lambda {{.*}}internal private dso_local @_ZZ2g3vENK3$_0clERKi(%[[THIS_ARG:.*]]: !cir.ptr<![[REC_LAM_G3:[^ >]*]]> {{.*}}, %[[REF_I_ARG:.*]]: !cir.ptr<!s32i> {{.*}})
// CIR:   %[[THIS_ALLOCA:.*]] = cir.alloca !cir.ptr<![[REC_LAM_G3]]>, !cir.ptr<!cir.ptr<![[REC_LAM_G3]]>>, ["this", init]
// CIR:   %[[REF_I_ALLOCA:.*]] = cir.alloca {{.*}} ["i", init, const]
// CIR:   cir.store %[[THIS_ARG]], %[[THIS_ALLOCA]]
// CIR:   cir.store %[[REF_I_ARG]], %[[REF_I_ALLOCA]]
// CIR:   %[[THIS:.*]] = cir.load %[[THIS_ALLOCA]]
// CIR:   %[[REF_I:.*]] = cir.load %[[REF_I_ALLOCA]]
// CIR:   %[[I:.*]] = cir.load{{.*}} %[[REF_I]]
// CIR:   cir.return %[[I]]

// LLVM: define internal noundef i32 @"_ZZ2g3vENK3$_0clERKi"(ptr noundef nonnull align 1 dereferenceable(1) %[[THIS_ARG:.*]], ptr noundef nonnull align 4 dereferenceable(4) %[[REF_I_ARG:.*]]){{.*}} {
// LLVM:   %[[THIS_ALLOCA:.*]] = alloca ptr
// LLVM:   %[[REF_I_ALLOCA:.*]] = alloca ptr
// LLVM:   store ptr %[[THIS_ARG]], ptr %[[THIS_ALLOCA]]
// LLVM:   store ptr %[[REF_I_ARG]], ptr %[[REF_I_ALLOCA]]
// LLVM:   %[[THIS:.*]] = load ptr, ptr %[[THIS_ALLOCA]]
// LLVM:   %[[REF_I:.*]] = load ptr, ptr %[[REF_I_ALLOCA]]
// LLVM:   %[[I:.*]] = load i32, ptr %[[REF_I]]
// LLVM:   ret i32 %[[I]]

// In OGCG, the _ZZ2g3vENK3$_0clERKi function is emitted after _ZZ2g3vEN3$_08__invokeERKi, see below.

// lambda invoker
// CIR: cir.func {{.*}}internal private dso_local @_ZZ2g3vEN3$_08__invokeERKi(%[[REF_I_ARG:.*]]: !cir.ptr<!s32i> {{.*}}) -> {{.*}}!s32i{{.*}} {
// CIR:   %[[REF_I_ALLOCA:.*]] = cir.alloca {{.*}} ["i", init, const]
// CIR:   %[[LAM_ALLOCA:.*]] = cir.alloca ![[REC_LAM_G3]], !cir.ptr<![[REC_LAM_G3]]>, ["unused.capture"]
// CIR:   cir.store %[[REF_I_ARG]], %[[REF_I_ALLOCA]]
// CIR:   %[[REF_I:.*]] = cir.load{{.*}} %[[REF_I_ALLOCA]]
// CIR:   %[[LAM_RESULT:.*]] = cir.call @_ZZ2g3vENK3$_0clERKi(%[[LAM_ALLOCA]], %[[REF_I]]) : (!cir.ptr<![[REC_LAM_G3]]>, !cir.ptr<!s32i>) -> !s32i
// CIR:   cir.return %[[LAM_RESULT]]

// LLVM: define internal noundef i32 @"_ZZ2g3vEN3$_08__invokeERKi"(ptr noundef nonnull align 4 dereferenceable(4) %[[REF_I_ARG:.*]]){{.*}} {
// LLVM:   %[[REF_I_ALLOCA:.*]] = alloca ptr
// LLVM:   %[[LAM_ALLOCA:.*]] = alloca %[[REC_LAM_G3:.*]],
// LLVM:   store ptr %[[REF_I_ARG]], ptr %[[REF_I_ALLOCA]]
// LLVM:   %[[REF_I:.*]] = load ptr, ptr %[[REF_I_ALLOCA]]
// LLVM:   %[[LAM_RESULT:.*]] = call noundef i32 @"_ZZ2g3vENK3$_0clERKi"(ptr noundef nonnull align 1 dereferenceable(1) %[[LAM_ALLOCA]], ptr noundef nonnull align 4 dereferenceable(4) %[[REF_I]])
// LLVM:   ret i32 %[[LAM_RESULT]]

// In OGCG, the _ZZ2g3vEN3$_08__invokeERKi function is emitted after _ZN1A3barEv, see below.

// lambda operator int (*)(int const&)()
// CIR:   cir.func {{.*}}internal private dso_local @_ZZ2g3vENK3$_0cvPFiRKiEEv(%[[THIS_ARG:.*]]: !cir.ptr<![[REC_LAM_G3]]> {{.*}}) -> {{.*}}!cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> !s32i>>{{.*}} {
// CIR:   %[[THIS_ALLOCA:.*]] = cir.alloca !cir.ptr<![[REC_LAM_G3]]>, !cir.ptr<!cir.ptr<![[REC_LAM_G3]]>>, ["this", init]
// CIR:   cir.store %[[THIS_ARG]], %[[THIS_ALLOCA]]
// CIR:   %[[THIS:.*]] = cir.load %[[THIS_ALLOCA]]
// CIR:   %[[INVOKER:.*]] = cir.get_global @_ZZ2g3vEN3$_08__invokeERKi : !cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> !s32i>>
// CIR:   cir.return %[[INVOKER]]

// LLVM: define internal noundef ptr @"_ZZ2g3vENK3$_0cvPFiRKiEEv"(ptr noundef nonnull align 1 dereferenceable(1) %[[THIS_ARG:.*]]){{.*}} {
// LLVM:  %[[THIS_ALLOCA:.*]] = alloca ptr
// LLVM:  store ptr %[[THIS_ARG]], ptr %[[THIS_ALLOCA]]
// LLVM:  %[[THIS:.*]] = load ptr, ptr %[[THIS_ALLOCA]]
// LLVM:  ret ptr @"_ZZ2g3vEN3$_08__invokeERKi"

// In OGCG, the _ZZ2g3vENK3$_0cvPFiRKiEEv function is emitted just after the _Z2g3v function, see above.

// CIR: cir.func{{.*}} @_Z2g3v() -> {{.*}}!s32i{{.*}} {
// CIR:   %[[FN_ADDR:.*]] = cir.alloca !cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> !s32i>>, !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> !s32i>>>, ["fn", init]
// CIR:   %[[TASK:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["task", init]

// 1. Use `operator int (*)(int const&)()` to retrieve the fnptr to `__invoke()`.
// CIR:     %[[SCOPE_RET:.*]] = cir.scope {
// CIR:       %[[LAM_ALLOCA:.*]] = cir.alloca ![[REC_LAM_G3]], !cir.ptr<![[REC_LAM_G3]]>, ["ref.tmp0"]
// CIR:       %[[OPERATOR_RESULT:.*]] = cir.call @_ZZ2g3vENK3$_0cvPFiRKiEEv(%[[LAM_ALLOCA]]){{.*}}
// CIR:       %[[PLUS:.*]] = cir.unary(plus, %[[OPERATOR_RESULT]])
// CIR:       cir.yield %[[PLUS]]
// CIR:     }

// 2. Load ptr to `__invoke()`.
// CIR:     cir.store{{.*}} %[[SCOPE_RET]], %[[FN_ADDR]]
// CIR:     %[[SCOPE_RET2:.*]] = cir.scope {
// CIR:       %[[REF_TMP1:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["ref.tmp1", init]
// CIR:       %[[FN:.*]] = cir.load{{.*}} %[[FN_ADDR]]
// CIR:       %[[THREE:.*]] = cir.const #cir.int<3> : !s32i
// CIR:       cir.store{{.*}} %[[THREE]], %[[REF_TMP1]]

// 3. Call `__invoke()`, which effectively executes `operator()`.
// CIR:       %[[RESULT:.*]] = cir.call %[[FN]](%[[REF_TMP1]]) {cir.call_arg_attrs = [{{.*cir.noundef.*}}]{{.*}}
// CIR:       cir.yield %[[RESULT]]
// CIR:     }

// CIR:     cir.store{{.*}} %[[SCOPE_RET2]], %[[TASK]]
// CIR:     %[[TASK_RET:.*]] = cir.load{{.*}} %[[TASK]]
// CIR:     cir.return %[[TASK_RET]]
// CIR:   }

// LLVM: define dso_local noundef i32 @_Z2g3v(){{.*}} {
// LLVM:   %[[LAM_ALLOCA:.*]] = alloca %[[REC_LAM_G3]]
// LLVM:   %[[REF_TMP1:.*]] = alloca i32
// LLVM:   %[[FN_PTR:.*]] = alloca ptr
// LLVM:   %[[TASK:.*]] = alloca i32
// LLVM:   %[[OPERATOR_RESULT:.*]] = call noundef ptr @"_ZZ2g3vENK3$_0cvPFiRKiEEv"(ptr noundef nonnull align 1 dereferenceable(1) %[[LAM_ALLOCA]])
// LLVM:   br label %[[SCOPE_BB1:.*]]

// LLVM: [[SCOPE_BB1]]:
// LLVM:   %[[TMP0:.*]] = phi ptr [ %[[OPERATOR_RESULT]], %{{.*}} ]
// LLVM:   store ptr %[[TMP0]], ptr %[[FN_PTR]]
// LLVM:   %[[FN:.*]] = load ptr, ptr %[[FN_PTR]]
// LLVM:   store i32 3, ptr %[[REF_TMP1]]
// LLVM:   %[[RESULT:.*]] = call noundef i32 %[[FN]](ptr noundef nonnull align 4 dereferenceable(4) %[[REF_TMP1]])
// LLVM:   br label %[[RET_BB:.*]]

// LLVM: [[RET_BB]]:
// LLVM:   %[[TMP1:.*]] = phi i32 [ %[[RESULT]], %[[SCOPE_BB1]] ]
// LLVM:   store i32 %[[TMP1]], ptr %[[TASK]]
// LLVM:   %[[TMP2:.*]] = load i32, ptr %[[TASK]]
// LLVM:   ret i32 %[[TMP2]]

// The definition for _Z2g3v in OGCG is first among the functions for the g3 test, see above.

// The functions below are emitted later in OGCG, see above for the corresponding LLVM checks.

// Non-trivial lambda invoker — emitted first in OGCG among the deferred functions
// OGCG: define internal void @"_ZN3$_28__invokeEv"(ptr dead_on_unwind noalias writable sret(%struct.NonTrivial) align 4 %[[AGG_RESULT:.*]])
// OGCG:   %[[CAPTURE_NT:.*]] = alloca %class.anon.2
// OGCG:   call void @"_ZNK3$_2clEv"(ptr dead_on_unwind writable sret(%struct.NonTrivial) align 4 %[[AGG_RESULT]], ptr {{.*}} %[[CAPTURE_NT]])

// Void lambda forwarding call — emitted between g3 conversion and g3 __invoke in OGCG
// OGCG: define internal void @"_ZZ6g_voidvEN3$_08__invokeEi"(i32 noundef
// OGCG:   call void @"_ZZ6g_voidvENK3$_0clEi"(
// OGCG:   ret void

// OGCG: define internal noundef i32 @"_ZZ2g3vEN3$_08__invokeERKi"(ptr {{.*}} %[[I_ARG:.*]])
// OGCG:   %[[I_ADDR:.*]] = alloca ptr
// OGCG:   %[[UNUSED_CAPTURE:.*]] = alloca %[[REC_LAM_G3:.*]]
// OGCG:   store ptr %[[I_ARG]], ptr %[[I_ADDR]]
// OGCG:   %[[I_PTR:.*]] = load ptr, ptr %[[I_ADDR]]
// OGCG:   %[[CALL:.*]] = call {{.*}} i32 @"_ZZ2g3vENK3$_0clERKi"(ptr {{.*}} %[[UNUSED_CAPTURE]], ptr {{.*}} %[[I_PTR]])
// OGCG:   ret i32 %[[CALL]]

// OGCG: define internal noundef i32 @"_ZZ2g3vENK3$_0clERKi"(ptr {{.*}} %[[THIS_ARG:.*]], ptr {{.*}} %[[I_ARG:.*]])
// OGCG:   %[[THIS_ADDR:.*]] = alloca ptr
// OGCG:   %[[I_ADDR:.*]] = alloca ptr
// OGCG:   store ptr %[[THIS_ARG]], ptr %[[THIS_ADDR]]
// OGCG:   store ptr %[[I_ARG]], ptr %[[I_ADDR]]
// OGCG:   %[[THIS:.*]] = load ptr, ptr %[[THIS_ADDR]]
// OGCG:   %[[I_PTR:.*]] = load ptr, ptr %[[I_ADDR]]
// OGCG:   %[[I:.*]] = load i32, ptr %[[I_PTR]]
// OGCG:   ret i32 %[[I]]

// Test: lambda forwarding a non-trivially-copyable aggregate return type.
// The invoker must forward the sret slot to operator() so we don't hit
// an aggregate-copy assertion on a non-trivially-copyable type.
struct NonTrivial {
  int x;
  NonTrivial() : x(0) {}
  NonTrivial(const NonTrivial &o) : x(o.x) {}
  NonTrivial &operator=(const NonTrivial &) = default;
};

NonTrivial (*fn_nontrivial)() = []() -> NonTrivial { return NonTrivial(); };

// The non-trivial invoker forwards the sret slot to operator().
// CIR: cir.func {{.*}}internal private dso_local @_ZN3$_28__invokeEv(%[[SRET_ARG:.*]]: !cir.ptr<![[REC_NT:.*]]>
// CIR-SAME: cir.sret
// CIR:   %[[CAPTURE:.*]] = cir.alloca {{.*}} ["unused.capture"]
// CIR:   cir.call @_ZNK3$_2clEv(%[[SRET_ARG]], %[[CAPTURE]])
// CIR:   cir.return

// LLVM: define internal void @"_ZN3$_28__invokeEv"(ptr dead_on_unwind noalias writable sret(%struct.NonTrivial) align 4 %[[SRET:.*]])
// LLVM:   %[[CAPTURE:.*]] = alloca %class.anon{{.*}}
// LLVM:   call void @"_ZNK3$_2clEv"(ptr dead_on_unwind writable sret(%struct.NonTrivial) align 4 %[[SRET]], ptr noundef nonnull align 1 dereferenceable(1) %[[CAPTURE]])
