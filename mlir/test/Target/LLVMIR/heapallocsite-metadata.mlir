// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s
// RUN: mlir-opt %s -split-input-file --verify-roundtrip -o /dev/null

#di_file = #llvm.di_file<"alloc.cpp" in "/">
#di_composite_type = #llvm.di_composite_type<tag = DW_TAG_structure_type,
  name = "S", file = #di_file, line = 1, sizeInBits = 32>

llvm.func @_Znwm(i64) -> !llvm.ptr

// CHECK-LABEL: @allocate
llvm.func @allocate(%size: i64) -> !llvm.ptr {
  // CHECK: call ptr @_Znwm(i64 %{{.*}}), !heapallocsite ![[TY:[0-9]+]]
  %0 = llvm.call @_Znwm(%size) {heapallocsite = #di_composite_type} : (i64) -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// CHECK: ![[TY]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S"

// -----

llvm.func @malloc(i64) -> !llvm.ptr

// An allocation of an unnamed type exports as the empty node, not as an absent
// attachment and not as a node with a null operand.

// CHECK-LABEL: @allocate_void
llvm.func @allocate_void(%size: i64) -> !llvm.ptr {
  // CHECK: call ptr @malloc(i64 %{{.*}}), !heapallocsite ![[EMPTY:[0-9]+]]
  %0 = llvm.call @malloc(%size) {heapallocsite = #llvm.di_null_type} : (i64) -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// CHECK: ![[EMPTY]] = !{}

// -----

#di_file = #llvm.di_file<"alloc.cpp" in "/">
#di_composite_type = #llvm.di_composite_type<tag = DW_TAG_structure_type,
  name = "S", file = #di_file, line = 1, sizeInBits = 32>

llvm.func @_Znwm(i64) -> !llvm.ptr
llvm.func @__gxx_personality_v0(...) -> i32

// CHECK-LABEL: @allocate_invoke
llvm.func @allocate_invoke(%size: i64) -> !llvm.ptr
    attributes {personality = @__gxx_personality_v0} {
  // CHECK: invoke ptr @_Znwm(i64 %{{.*}})
  // CHECK: !heapallocsite ![[TY:[0-9]+]]
  %0 = llvm.invoke @_Znwm(%size) to ^bb1 unwind ^bb2
      {heapallocsite = #di_composite_type} : (i64) -> !llvm.ptr
^bb1:
  llvm.return %0 : !llvm.ptr
^bb2:
  %1 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  %2 = llvm.mlir.zero : !llvm.ptr
  llvm.return %2 : !llvm.ptr
}

// CHECK: ![[TY]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S"
