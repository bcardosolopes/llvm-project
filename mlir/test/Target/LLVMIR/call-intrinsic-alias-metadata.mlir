// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-alias-call-intrinsic.ll: llvm.call_intrinsic
// has to write its alias metadata back out, the way the call and invoke paths do.
// The body is exactly what the importer produces for that input.

// CHECK: call ptr @llvm.load.relative.i32(ptr %{{.*}}, i32 8)
// CHECK-SAME: !tbaa ![[TBAA:[0-9]+]]
// CHECK-SAME: !alias.scope ![[SCOPE:[0-9]+]]
// CHECK-SAME: !noalias ![[NOALIAS:[0-9]+]]
// CHECK-DAG: ![[TBAA]] = !{![[PTR:[0-9]+]], ![[PTR]], i64 0}
// CHECK-DAG: ![[SCOPE]] = !{!{{[0-9]+}}}
// CHECK-DAG: ![[NOALIAS]] = !{!{{[0-9]+}}}

#alias_scope_domain = #llvm.alias_scope_domain<id = distinct[0]<>, description = "vcall">
#tbaa_root = #llvm.tbaa_root<id = "Simple C++ TBAA">
#alias_scope = #llvm.alias_scope<id = distinct[1]<>, domain = #alias_scope_domain, description = "vcall: %fn">
#alias_scope1 = #llvm.alias_scope<id = distinct[2]<>, domain = #alias_scope_domain, description = "vcall: %vtable">
#tbaa_type_desc = #llvm.tbaa_type_desc<id = "omnipotent char", members = {<typeDesc = #tbaa_root, offset = 0>}>
#tbaa_type_desc1 = #llvm.tbaa_type_desc<id = "any pointer", members = {<typeDesc = #tbaa_type_desc, offset = 0>}>
#tbaa_tag = #llvm.tbaa_tag<base_type = #tbaa_type_desc1, access_type = #tbaa_type_desc1, offset = 0>
module {
  llvm.func @llvm.load.relative.i32(!llvm.ptr, i32) -> !llvm.ptr attributes {memory_effects = #llvm.memory_effects<other = none, argMem = read, inaccessibleMem = none, errnoMem = none, targetMem0 = none, targetMem1 = none>, no_unwind, passthrough = ["nocallback", "nofree", "nosync"], will_return}
  llvm.func @vcall(%arg0: !llvm.ptr) -> !llvm.ptr {
    %0 = llvm.mlir.constant(8 : i32) : i32
    %1 = llvm.call_intrinsic "llvm.load.relative.i32"(%arg0, %0) {alias_scopes = [#alias_scope], noalias_scopes = [#alias_scope1], tbaa = [#tbaa_tag]} : (!llvm.ptr, i32) -> !llvm.ptr
    llvm.return %1 : !llvm.ptr
  }
}
