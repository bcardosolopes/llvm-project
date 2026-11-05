// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s
// RUN: mlir-opt %s -split-input-file --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-coverage-gaps.ll.

#alias_scope_domain = #llvm.alias_scope_domain<id = distinct[0]<>, description = "prefetch_noalias">
#alias_scope = #llvm.alias_scope<id = distinct[1]<>, domain = #alias_scope_domain, description = "prefetch_noalias: %p">

// CHECK: call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 1, i32 1), !noalias ![[NA:[0-9]+]]
// CHECK: ![[NA]] = !{!{{[0-9]+}}}
llvm.func @prefetch_noalias(%arg0: !llvm.ptr) {
  "llvm.intr.prefetch"(%arg0) <{cache = 1 : i32, hint = 1 : i32,
                                noalias_scopes = [#alias_scope],
                                rw = 0 : i32}> : (!llvm.ptr) -> ()
  llvm.return
}

// -----

// A function carries !annotation on the GlobalObject, not on an instruction.

// CHECK: define void @annotated_function() !annotation ![[A:[0-9]+]]
// CHECK: ![[A]] = !{!"auto-init"}
llvm.func @annotated_function() attributes {annotations = ["auto-init"]} {
  llvm.return
}

// -----

// CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !irr_loop ![[I:[0-9]+]]
// CHECK: ![[I]] = !{!"loop_header_weight", i64 100}
llvm.func @irreducible_header(%c: i1) {
  llvm.br ^hdr
^hdr:
  llvm.cond_br %c, ^hdr, ^exit {irr_loop_header_weight = 100 : i64}
^exit:
  llvm.return
}
