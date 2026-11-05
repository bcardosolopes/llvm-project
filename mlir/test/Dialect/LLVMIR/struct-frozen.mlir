// RUN: mlir-opt %s | FileCheck %s
// RUN: mlir-opt %s | mlir-opt | FileCheck %s
// RUN: mlir-opt %s --emit-bytecode | mlir-opt | FileCheck %s

// Identified structs that are not recursive are uniqued by name, opacity,
// packedness and body. The order of the declarations does not matter.

// CHECK: llvm.mlir.global internal @def0() {{.*}} : !llvm.struct<"S", (i32)>
// CHECK: llvm.mlir.global external @decl() {{.*}} : !llvm.struct<"S", opaque>
// CHECK: llvm.mlir.global internal @def1() {{.*}} : !llvm.struct<"S", (i64)>
// CHECK: llvm.mlir.global internal @packed() {{.*}} : !llvm.struct<"S", packed (i32)>
// CHECK: llvm.mlir.global internal @empty() {{.*}} : !llvm.struct<"S", ()>
// CHECK: llvm.mlir.global external @dotted() {{.*}} : !llvm.struct<"S.0", opaque>
// CHECK-NOT: "S.1"
llvm.mlir.global internal @def0() : !llvm.struct<"S", (i32)>
llvm.mlir.global external @decl() : !llvm.struct<"S", opaque>
llvm.mlir.global internal @def1() : !llvm.struct<"S", (i64)>
llvm.mlir.global internal @packed() : !llvm.struct<"S", packed (i32)>
llvm.mlir.global internal @empty() : !llvm.struct<"S", ()>
llvm.mlir.global external @dotted() : !llvm.struct<"S.0", opaque>

// The same text gives the same type, so the return type matches.
// CHECK-LABEL: llvm.func @same
llvm.func @same(%arg0: !llvm.struct<"S", (i32)>) -> !llvm.struct<"S", (i32)> {
  llvm.return %arg0 : !llvm.struct<"S", (i32)>
}

// Mutually recursive structs round-trip from either member.
// CHECK: llvm.func @cycle_r(!llvm.struct<"R", (struct<"Q", (struct<"R">, i32)>, i64)>)
// CHECK: llvm.func @cycle_q(!llvm.struct<"Q", (struct<"R", (struct<"Q">, i64)>, i32)>)
llvm.func @cycle_r(!llvm.struct<"R", (struct<"Q", (struct<"R">, i32)>, i64)>)
llvm.func @cycle_q(!llvm.struct<"Q", (struct<"R", (struct<"Q">, i64)>, i32)>)

// A struct whose body holds a different struct with the same name prints the
// inner struct in full, and that text parses back to the same types.
// CHECK: llvm.func @nested_same_name(!llvm.struct<"A", (struct<"A", (i32)>)>)
!inner = !llvm.struct<"A", (i32)>
!outer = !llvm.struct<"A", (!inner)>
llvm.func @nested_same_name(!outer)

// A recursive struct can hold a frozen struct with the same name.
// CHECK: llvm.func @recursive_holds_same_name(!llvm.struct<"B", (struct<"B">, struct<"B", (i32)>)>)
llvm.func @recursive_holds_same_name(!llvm.struct<"B", (struct<"B">, struct<"B", (i32)>)>)
