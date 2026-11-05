// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-alias-fence.ll.

// CHECK: fence seq_cst, !noalias ![[NA:[0-9]+]]
// CHECK: ![[NA]] = !{!{{[0-9]+}}}

#alias_scope_domain = #llvm.alias_scope_domain<id = distinct[0]<>, description = "fenced">
#alias_scope = #llvm.alias_scope<id = distinct[1]<>, domain = #alias_scope_domain, description = "fenced: %p">
module {
  llvm.func @fenced() {
    llvm.fence seq_cst {noalias_scopes = [#alias_scope]}
    llvm.return
  }
}
