// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-alias-intrinsics.ll. The body is exactly
// what the importer produces, so the two pin the whole trip. All eight calls
// must come back carrying !noalias.

// CHECK-COUNT-8: !noalias !

#alias_scope_domain = #llvm.alias_scope_domain<id = distinct[0]<>, description = "carriers">
#alias_scope = #llvm.alias_scope<id = distinct[1]<>, domain = #alias_scope_domain, description = "carriers: %p">
module {
  llvm.func @carriers(%arg0: i1, %arg1: !llvm.ptr, %arg2: !llvm.ptr, ...) {
    llvm.intr.experimental.noalias.scope.decl #alias_scope {noalias_scopes = [#alias_scope]}
    llvm.intr.assume %arg0  : i1 {noalias_scopes = [#alias_scope]}
    llvm.intr.vastart %arg1 {noalias_scopes = [#alias_scope]} : !llvm.ptr
    llvm.intr.vacopy %arg2 to %arg1 {noalias_scopes = [#alias_scope]} : !llvm.ptr, !llvm.ptr
    llvm.intr.vaend %arg1 {noalias_scopes = [#alias_scope]} : !llvm.ptr
    %0 = llvm.intr.stacksave {noalias_scopes = [#alias_scope]} : !llvm.ptr
    llvm.intr.stackrestore %0 {noalias_scopes = [#alias_scope]} : !llvm.ptr
    "llvm.intr.trap"() <{noalias_scopes = [#alias_scope]}> : () -> ()
    llvm.return
  }
}
