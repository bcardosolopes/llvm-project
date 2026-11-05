; RUN: mlir-translate -import-llvm -split-input-file %s | FileCheck %s

; CHECK: #[[DOMAIN:.*]] = #llvm.alias_scope_domain<id = {{.*}}, description = "The domain">
; CHECK: #[[$SCOPE0:.*]] = #llvm.alias_scope<id = {{.*}}, domain = #[[DOMAIN]], description = "The first scope">
; CHECK: #[[$SCOPE1:.*]] = #llvm.alias_scope<id = {{.*}}, domain = #[[DOMAIN]]>
; CHECK: #[[$SCOPE2:.*]] = #llvm.alias_scope<id = {{.*}}, domain = #[[DOMAIN]]>

; CHECK-LABEL: llvm.func @alias_scope
define void @alias_scope(ptr %arg1) {
  ; CHECK: llvm.load
  ; CHECK-SAME:  alias_scopes = [#[[$SCOPE0]]]
  ; CHECK-SAME:  noalias_scopes = [#[[$SCOPE1]], #[[$SCOPE2]]]
  %1 = load i32, ptr %arg1, !alias.scope !4, !noalias !7
  ; CHECK: llvm.load
  ; CHECK-SAME:  alias_scopes = [#[[$SCOPE1]]]
  ; CHECK-SAME:  noalias_scopes = [#[[$SCOPE0]], #[[$SCOPE2]]]
  %2 = load i32, ptr %arg1, !alias.scope !5, !noalias !8
  ; CHECK: llvm.load
  ; CHECK-SAME:  alias_scopes = [#[[$SCOPE2]]]
  ; CHECK-SAME:  noalias_scopes = [#[[$SCOPE0]], #[[$SCOPE1]]]
  %3 = load i32, ptr %arg1, !alias.scope !6, !noalias !9
  ret void
}

!0 = distinct !{!0, !"The domain"}
!1 = distinct !{!1, !0, !"The first scope"}
!2 = distinct !{!2, !0}
!3 = distinct !{!3, !0}
!4 = !{!1}
!5 = !{!2}
!6 = !{!3}
!7 = !{!2, !3}
!8 = !{!1, !3}
!9 = !{!1, !2}

; // -----

; CHECK: #[[DOMAIN0:.*]] = #llvm.alias_scope_domain<id = {{.*}}, description = "The domain">
; CHECK: #[[DOMAIN1:.*]] = #llvm.alias_scope_domain<id = {{.*}}>
; CHECK: #[[$SCOPE0:.*]] = #llvm.alias_scope<id = {{.*}}, domain = #[[DOMAIN0]]>
; CHECK: #[[$SCOPE1:.*]] = #llvm.alias_scope<id = {{.*}}, domain = #[[DOMAIN1]]>

; CHECK-LABEL: llvm.func @two_domains
define void @two_domains(ptr %arg1) {
  ; CHECK: llvm.load
  ; CHECK-SAME:  alias_scopes = [#[[$SCOPE0]]]
  ; CHECK-SAME:  noalias_scopes = [#[[$SCOPE1]]]
  %1 = load i32, ptr %arg1, !alias.scope !4, !noalias !5
  ret void
}

!0 = distinct !{!0, !"The domain"}
!1 = distinct !{!1}
!2 = !{!2, !0}
!3 = !{!3, !1}
!4 = !{!2}
!5 = !{!3}

; // -----

; CHECK: #[[DOMAIN:.*]] = #llvm.alias_scope_domain<id = {{.*}}, description = "The domain">
; CHECK: #[[$SCOPE:.*]] = #llvm.alias_scope<id = {{.*}}, domain = #[[DOMAIN]]>

; CHECK-LABEL: llvm.func @supported_ops
define void @supported_ops(ptr %arg1, float %arg2, i32 %arg3, i32 %arg4) {
  ; CHECK: llvm.intr.experimental.noalias.scope.decl #[[$SCOPE]]
  call void @llvm.experimental.noalias.scope.decl(metadata !2)
  ; CHECK: llvm.load {{.*}}alias_scopes = [#[[$SCOPE]]]
  %1 = load i32, ptr %arg1, !alias.scope !2
  ; CHECK: llvm.store {{.*}}alias_scopes = [#[[$SCOPE]]]
  store i32 %1, ptr %arg1, !alias.scope !2
  ; CHECK: llvm.atomicrmw {{.*}}alias_scopes = [#[[$SCOPE]]]
  %2 = atomicrmw fmax ptr %arg1, float %arg2 acquire, !alias.scope !2
  ; CHECK: llvm.cmpxchg {{.*}}alias_scopes = [#[[$SCOPE]]]
  %3 = cmpxchg ptr %arg1, i32 %arg3, i32 %arg4 monotonic seq_cst, !alias.scope !2
  ; CHECK: "llvm.intr.memcpy"{{.*}}alias_scopes = [#[[$SCOPE]]]
  call void @llvm.memcpy.p0.p0.i32(ptr %arg1, ptr %arg1, i32 4, i1 false), !alias.scope !2
  ; CHECK: "llvm.intr.memset"{{.*}}alias_scopes = [#[[$SCOPE]]]
  call void @llvm.memset.p0.i32(ptr %arg1, i8 42, i32 4, i1 false), !alias.scope !2
  ; CHECK: llvm.call{{.*}}alias_scopes = [#[[$SCOPE]]]
  call void @foo(ptr %arg1), !alias.scope !2
  ; CHECK: llvm.call{{.*}}noalias_scopes = [#[[$SCOPE]]]
  call void @foo(ptr %arg1), !noalias !2
  ret void
}

declare void @llvm.experimental.noalias.scope.decl(metadata)
declare void @llvm.memcpy.p0.p0.i32(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i32, i1 immarg)
declare void @llvm.memset.p0.i32(ptr nocapture writeonly, i8, i32, i1 immarg)
declare void @foo(ptr %arg1)

!0 = distinct !{!0, !"The domain"}
!1 = !{!1, !0}
!2 = !{!1}

; // -----

; CHECK: #[[DOMAIN:.*]] = #llvm.alias_scope_domain<id = "domain1">
; CHECK: #[[$SCOPE0:.*]] = #llvm.alias_scope<id = "scopeid1", domain = #[[DOMAIN]], description = "The first scope">
; CHECK: #[[$SCOPE1:.*]] = #llvm.alias_scope<id = "scopeid2", domain = #[[DOMAIN]]>
; CHECK: #[[$SCOPE2:.*]] = #llvm.alias_scope<id = "scopeid3", domain = #[[DOMAIN]]>

; CHECK-LABEL: llvm.func @alias_scope
define void @alias_scope(ptr %arg1) {
  ; CHECK: llvm.load
  ; CHECK-SAME:  alias_scopes = [#[[$SCOPE0]]]
  ; CHECK-SAME:  noalias_scopes = [#[[$SCOPE1]], #[[$SCOPE2]]]
  %1 = load i32, ptr %arg1, !alias.scope !4, !noalias !7
  ; CHECK: llvm.load
  ; CHECK-SAME:  alias_scopes = [#[[$SCOPE1]]]
  ; CHECK-SAME:  noalias_scopes = [#[[$SCOPE0]], #[[$SCOPE2]]]
  %2 = load i32, ptr %arg1, !alias.scope !5, !noalias !8
  ; CHECK: llvm.load
  ; CHECK-SAME:  alias_scopes = [#[[$SCOPE2]]]
  ; CHECK-SAME:  noalias_scopes = [#[[$SCOPE0]], #[[$SCOPE1]]]
  %3 = load i32, ptr %arg1, !alias.scope !6, !noalias !9
  ret void
}

!0 = !{!"domain1"}
!1 = !{!"scopeid1", !0, !"The first scope"}
!2 = !{!"scopeid2", !0}
!3 = !{!"scopeid3", !0}
!4 = !{!1}
!5 = !{!2}
!6 = !{!3}
!7 = !{!2, !3}
!8 = !{!1, !3}
!9 = !{!1, !2}

; // -----

; An invoke carries alias metadata exactly as a call does, and so do the
; lifetime and invariant.start markers. Before these ops declared
; AliasAnalysisOpInterface the importer had nowhere to put the metadata and
; dropped it; on a merged Android library that was 710 attachments in one .so.

declare void @callee(ptr)
declare i32 @__gxx_personality_v0(...)

; CHECK-DAG: #[[$SCOPE:.*]] = #llvm.alias_scope<{{.*}}>
define void @invoke_and_markers(ptr %arg1) personality ptr @__gxx_personality_v0 {
  %a = alloca i32
  ; CHECK: llvm.intr.lifetime.start
  ; CHECK-SAME: alias_scopes = [#[[$SCOPE]]]
  call void @llvm.lifetime.start.p0(ptr %a), !alias.scope !2
  ; CHECK: llvm.intr.invariant.start
  ; CHECK-SAME: alias_scopes = [#[[$SCOPE]]]
  %i = call ptr @llvm.invariant.start.p0(i64 4, ptr %a), !alias.scope !2
  ; CHECK: llvm.invoke
  ; CHECK-SAME: alias_scopes = [#[[$SCOPE]]]
  invoke void @callee(ptr %arg1) to label %ok unwind label %lp, !alias.scope !2
ok:
  ; CHECK: llvm.intr.lifetime.end
  ; CHECK-SAME: alias_scopes = [#[[$SCOPE]]]
  call void @llvm.lifetime.end.p0(ptr %a), !alias.scope !2
  ret void
lp:
  %e = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %e
}

declare void @llvm.lifetime.start.p0(ptr nocapture)
declare void @llvm.lifetime.end.p0(ptr nocapture)
declare ptr @llvm.invariant.start.p0(i64 immarg, ptr nocapture)

!0 = !{!"domain"}
!1 = !{!"scope", !0}
!2 = !{!1}
