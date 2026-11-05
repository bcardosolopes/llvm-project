// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-alias-ubsantrap.ll. The body is exactly
// what the importer produces, so the two pin the whole trip.

// CHECK: call void @llvm.ubsantrap(i8 2), !noalias ![[NA:[0-9]+]], !nosanitize ![[NS:[0-9]+]]
// CHECK-DAG: ![[NA]] = !{!{{[0-9]+}}}
// CHECK-DAG: ![[NS]] = !{}

#alias_scope_domain = #llvm.alias_scope_domain<id = distinct[0]<>, description = "checked">
#alias_scope = #llvm.alias_scope<id = distinct[1]<>, domain = #alias_scope_domain, description = "checked: %p">
module {
  llvm.func @checked() {
    "llvm.intr.ubsantrap"() <{failureKind = 2 : i8, noalias_scopes = [#alias_scope]}> {llvm.nosanitize} : () -> ()
    llvm.return
  }
}
