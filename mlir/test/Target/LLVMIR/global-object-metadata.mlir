// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-global-object.ll. The body is exactly
// what the importer produces, so the two pin the whole trip.
//
// !associated is written only once every global and function exists, since it
// names another symbol; the others are written as each GlobalObject is created.

// CHECK-DAG: @g = global i32 0, !absolute_symbol ![[ABS:[0-9]+]], !exclude ![[EXC:[0-9]+]]
// CHECK-DAG: @h = global i32 1, !associated ![[ASSOC:[0-9]+]]
// CHECK-DAG: @vt = global [1 x ptr] zeroinitializer, !vcall_visibility ![[VIS:[0-9]+]]
// CHECK-DAG: define void @f() !section_prefix ![[PFX:[0-9]+]] !kcfi_type ![[KCFI:[0-9]+]]
// CHECK-DAG: ![[ABS]] = !{i64 0, i64 256}
// CHECK-DAG: ![[EXC]] = !{}
// CHECK-DAG: ![[ASSOC]] = !{ptr @g}
// CHECK-DAG: ![[VIS]] = !{i64 1}
// CHECK-DAG: @cold = global i32 2, !section_prefix ![[COLD:[0-9]+]]
// CHECK-DAG: ![[PFX]] = !{!"section_prefix", !"hot"}
// CHECK-DAG: ![[COLD]] = !{!"section_prefix", !"unlikely"}
// CHECK-DAG: define void @e() !section_prefix ![[EMPTY:[0-9]+]]
// CHECK-DAG: ![[EMPTY]] = !{!"section_prefix", !""}
// The verifier fixes !kcfi_type at 32 bits.
// CHECK-DAG: ![[KCFI]] = !{i32 12345}

module {
  llvm.mlir.global external @g(0 : i32) {absolute_symbol = #llvm.constant_range<i64, 0, 256>, addr_space = 0 : i32, exclude} : i32
  llvm.mlir.global external @h(1 : i32) {addr_space = 0 : i32, associated = @g} : i32
  llvm.mlir.global external @vt() {addr_space = 0 : i32, vcall_visibility = 1 : i64} : !llvm.array<1 x ptr> {
    %0 = llvm.mlir.zero : !llvm.ptr
    %1 = llvm.mlir.undef : !llvm.array<1 x ptr>
    %2 = llvm.insertvalue %0, %1[0] : !llvm.array<1 x ptr> 
    llvm.return %2 : !llvm.array<1 x ptr>
  }
  llvm.mlir.global external @cold(2 : i32) {addr_space = 0 : i32, section_prefix = "unlikely"} : i32
  llvm.func @f() attributes {kcfi_type = 12345 : i32, section_prefix = "hot"} {
    llvm.return
  }
  llvm.func @e() attributes {section_prefix = ""} {
    llvm.return
  }
}
