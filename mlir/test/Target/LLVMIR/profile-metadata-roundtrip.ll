; Branch weights are consumed by plain target codegen -- BranchProbabilityInfo
; reads MD_prof, and SelectionDAGISel requires it -- so they must survive
; LLVM IR -> LLVM dialect -> LLVM IR. The translation must also reach a fixed
; point: whatever the first round trip produces has to be stable under further
; round trips, so a pipeline that imports and re-exports repeatedly (as a
; link-time optimizer may) cannot keep eroding the profile.

; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir | FileCheck %s

; Fixed point: pass 1 and pass 2 must be byte-identical.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir > %t.pass1.ll
; RUN: mlir-translate -import-llvm %t.pass1.ll | mlir-translate -mlir-to-llvmir > %t.pass2.ll
; RUN: diff %t.pass1.ll %t.pass2.ll

target triple = "aarch64-none-linux-android21"

; Weights from __builtin_expect carry an "expected" marker before the weights.
; CHECK: define i32 @expect_weights(
define i32 @expect_weights(i1 %c, i32 %a) {
  ; CHECK: br i1 {{.*}}, !prof ![[EXPECT:[0-9]+]]
  br i1 %c, label %t, label %f, !prof !0
t:
  ret i32 %a
f:
  ret i32 0
}

; CHECK: define i32 @plain_weights(
define i32 @plain_weights(i1 %c, i32 %a) {
  ; CHECK: br i1 {{.*}}, !prof ![[PLAIN:[0-9]+]]
  br i1 %c, label %t, label %f, !prof !1
t:
  ret i32 %a
f:
  ret i32 0
}

; Value profiles: indirect call targets on a call and an invoke, memory
; intrinsic sizes, and vtable targets on a load. The full-LTO link's indirect
; call promotion and memory intrinsic size specialization read them.
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare i32 @__gxx_personality_v0(...)

; CHECK: define void @value_profiles(
define void @value_profiles(ptr %f, ptr %d, ptr %s, i64 %n) personality ptr @__gxx_personality_v0 {
  ; CHECK: call void %{{.*}}(), !prof ![[CALLVP:[0-9]+]]
  call void %f(), !prof !3
  ; CHECK: call void @llvm.memcpy{{.*}}, !prof ![[MEMOPVP:[0-9]+]]
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false), !prof !4
  ; CHECK: load ptr, ptr {{.*}}, !prof ![[VTABLEVP:[0-9]+]]
  %vtable = load ptr, ptr %d, !prof !5
  ; CHECK: invoke void %{{.*}}()
  ; CHECK-NEXT: to label {{.*}} unwind label {{.*}}, !prof ![[INVOKEVP:[0-9]+]]
  invoke void %vtable() to label %ok unwind label %lp, !prof !6
ok:
  ret void
lp:
  %l = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %l
}

; A local function's profile name, which maps value profile target hashes to
; it in a full-LTO link.
; CHECK: define internal void @local_target() !PGOFuncName ![[PGONAME:[0-9]+]]
define internal void @local_target() !PGOFuncName !7 {
  ret void
}

define ptr @take_local_target() {
  ret ptr @local_target
}

; The "expected" marker has no LLVM dialect representation, so it is not
; re-emitted -- but the weights it guarded are, and both forms converge on the
; same shape.
; CHECK-DAG: ![[EXPECT]] = !{!"branch_weights", i32 2000, i32 1}
; CHECK-DAG: ![[PLAIN]] = !{!"branch_weights", i32 7, i32 3}
; CHECK-DAG: ![[CALLVP]] = !{!"VP", i32 0, i64 1600, i64 -7046029254386353131, i64 1500, i64 12345, i64 100}
; CHECK-DAG: ![[MEMOPVP]] = !{!"VP", i32 1, i64 100, i64 8, i64 90, i64 16, i64 10}
; CHECK-DAG: ![[VTABLEVP]] = !{!"VP", i32 2, i64 7, i64 99, i64 7}
; CHECK-DAG: ![[INVOKEVP]] = !{!"VP", i32 0, i64 50, i64 42, i64 50}
; CHECK-DAG: ![[PGONAME]] = !{!"a.cpp;local_target"}

!0 = !{!"branch_weights", !"expected", i32 2000, i32 1}
!1 = !{!"branch_weights", i32 7, i32 3}
!3 = !{!"VP", i32 0, i64 1600, i64 -7046029254386353131, i64 1500, i64 12345, i64 100}
!4 = !{!"VP", i32 1, i64 100, i64 8, i64 90, i64 16, i64 10}
!5 = !{!"VP", i32 2, i64 7, i64 99, i64 7}
!6 = !{!"VP", i32 0, i64 50, i64 42, i64 50}
!7 = !{!"a.cpp;local_target"}
