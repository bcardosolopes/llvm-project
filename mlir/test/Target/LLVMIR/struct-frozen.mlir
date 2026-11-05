// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// Two parsed structs with the same name and different bodies are different
// types. LLVM IR gives the second one a new name.

// CHECK-DAG: %[[S0:S(\.[0-9]+)?]] = type { i32 }
// CHECK-DAG: %[[S1:S(\.[0-9]+)?]] = type { i64 }
// CHECK-DAG: @def0 = internal global %[[S0]] undef
// CHECK-DAG: @def1 = internal global %[[S1]] undef
// CHECK-DAG: @same = internal global %[[S0]] undef

llvm.mlir.global internal @def0() : !llvm.struct<"S", (i32)> {
  %0 = llvm.mlir.undef : !llvm.struct<"S", (i32)>
  llvm.return %0 : !llvm.struct<"S", (i32)>
}
llvm.mlir.global internal @def1() : !llvm.struct<"S", (i64)> {
  %0 = llvm.mlir.undef : !llvm.struct<"S", (i64)>
  llvm.return %0 : !llvm.struct<"S", (i64)>
}
llvm.mlir.global internal @same() : !llvm.struct<"S", (i32)> {
  %0 = llvm.mlir.undef : !llvm.struct<"S", (i32)>
  llvm.return %0 : !llvm.struct<"S", (i32)>
}
