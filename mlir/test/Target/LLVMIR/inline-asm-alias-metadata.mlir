// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-alias-inline-asm.ll. The body is exactly
// what the importer produces for that input, so the two pin the whole trip.

// CHECK: tail call i64 asm sideeffect "mov $0, sp", "=r"()
// CHECK-SAME: !alias.scope ![[SCOPE:[0-9]+]]
// CHECK-SAME: !noalias ![[NOALIAS:[0-9]+]]
// CHECK-SAME: !srcloc ![[SRCLOC:[0-9]+]]
// CHECK-DAG: ![[SCOPE]] = !{!{{[0-9]+}}}
// CHECK-DAG: ![[NOALIAS]] = !{!{{[0-9]+}}}
// CHECK-DAG: ![[SRCLOC]] = !{i64 77}

#alias_scope_domain = #llvm.alias_scope_domain<id = distinct[0]<>, description = "sp">
#alias_scope = #llvm.alias_scope<id = distinct[1]<>, domain = #alias_scope_domain, description = "sp: %b">
#alias_scope1 = #llvm.alias_scope<id = distinct[2]<>, domain = #alias_scope_domain, description = "sp: %a">
module {
  llvm.func @sp() -> i64 {
    %0 = llvm.inline_asm has_side_effects tail_call_kind = <tail> asm_dialect = att {alias_scopes = [#alias_scope], noalias_scopes = [#alias_scope1], srcloc = array<i64: 77>} "mov $0, sp", "=r"  : () -> i64
    llvm.return %0 : i64
  }
}
