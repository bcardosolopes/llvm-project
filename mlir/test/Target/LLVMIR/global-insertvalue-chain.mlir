// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// A chain of insertvalue operations folds to the aggregate with every value
// inserted, whatever positions the chain writes and in whatever order.

// CHECK: @array = global [4 x i32] [i32 1, i32 2, i32 undef, i32 4]
llvm.mlir.global @array() : !llvm.array<4 x i32> {
  %0 = llvm.mlir.undef : !llvm.array<4 x i32>
  %1 = llvm.mlir.constant(1 : i32) : i32
  %2 = llvm.mlir.constant(2 : i32) : i32
  %3 = llvm.mlir.constant(4 : i32) : i32
  %4 = llvm.insertvalue %3, %0[3] : !llvm.array<4 x i32>
  %5 = llvm.insertvalue %2, %4[1] : !llvm.array<4 x i32>
  %6 = llvm.insertvalue %1, %5[0] : !llvm.array<4 x i32>
  llvm.return %6 : !llvm.array<4 x i32>
}

// A later insert overwrites an earlier one at the same position, including a
// whole nested aggregate written before one of its elements.
// CHECK: @nested = global { i32, [2 x i64] }
// CHECK-SAME: { i32 7, [2 x i64] [i64 0, i64 9] }
llvm.mlir.global @nested() : !llvm.struct<(i32, array<2 x i64>)> {
  %0 = llvm.mlir.zero : !llvm.struct<(i32, array<2 x i64>)>
  %1 = llvm.mlir.constant(3 : i32) : i32
  %2 = llvm.mlir.constant(7 : i32) : i32
  %3 = llvm.mlir.constant(9 : i64) : i64
  %4 = llvm.mlir.zero : !llvm.array<2 x i64>
  %5 = llvm.mlir.constant(5 : i64) : i64
  %6 = llvm.insertvalue %1, %0[0] : !llvm.struct<(i32, array<2 x i64>)>
  %7 = llvm.insertvalue %5, %6[1, 0] : !llvm.struct<(i32, array<2 x i64>)>
  %8 = llvm.insertvalue %4, %7[1] : !llvm.struct<(i32, array<2 x i64>)>
  %9 = llvm.insertvalue %3, %8[1, 1] : !llvm.struct<(i32, array<2 x i64>)>
  %10 = llvm.insertvalue %2, %9[0] : !llvm.struct<(i32, array<2 x i64>)>
  llvm.return %10 : !llvm.struct<(i32, array<2 x i64>)>
}

// An insertvalue whose result has another user ends a chain; the users see
// the aggregate as it is at that point.
// CHECK: @shared = global { [2 x i8], [2 x i8] }
// CHECK-SAME: { [2 x i8] c"\01\00", [2 x i8] c"\01\02" }
llvm.mlir.global @shared() : !llvm.struct<(array<2 x i8>, array<2 x i8>)> {
  %0 = llvm.mlir.zero : !llvm.array<2 x i8>
  %1 = llvm.mlir.constant(1 : i8) : i8
  %2 = llvm.mlir.constant(2 : i8) : i8
  %3 = llvm.insertvalue %1, %0[0] : !llvm.array<2 x i8>
  %4 = llvm.insertvalue %2, %3[1] : !llvm.array<2 x i8>
  %5 = llvm.mlir.undef : !llvm.struct<(array<2 x i8>, array<2 x i8>)>
  %6 = llvm.insertvalue %3, %5[0] : !llvm.struct<(array<2 x i8>, array<2 x i8>)>
  %7 = llvm.insertvalue %4, %6[1] : !llvm.struct<(array<2 x i8>, array<2 x i8>)>
  llvm.return %7 : !llvm.struct<(array<2 x i8>, array<2 x i8>)>
}

// An empty position replaces the whole aggregate. A chain with one is
// converted one insertvalue at a time instead of folded at once, with the same
// result.
// CHECK: @empty_position = global [2 x i32] [i32 0, i32 2]
llvm.mlir.global @empty_position() : !llvm.array<2 x i32> {
  %0 = llvm.mlir.undef : !llvm.array<2 x i32>
  %1 = llvm.mlir.zero : !llvm.array<2 x i32>
  %2 = llvm.mlir.constant(1 : i32) : i32
  %3 = llvm.mlir.constant(2 : i32) : i32
  %4 = llvm.insertvalue %2, %0[0] : !llvm.array<2 x i32>
  %5 = llvm.insertvalue %1, %4[] : !llvm.array<2 x i32>
  %6 = llvm.insertvalue %3, %5[1] : !llvm.array<2 x i32>
  llvm.return %6 : !llvm.array<2 x i32>
}
