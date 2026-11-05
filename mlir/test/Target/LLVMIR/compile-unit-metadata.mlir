// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

// A compile unit carried on the module rather than by a subprogram. Exporting
// it registers it in `!llvm.dbg.cu`, which is where it came from.

#di_file = #llvm.di_file<"alloc.cpp" in "/">
#di_compile_unit = #llvm.di_compile_unit<
  id = distinct[0]<>, sourceLanguage = DW_LANG_C_plus_plus_14,
  file = #di_file, producer = "clang", isOptimized = false,
  emissionKind = Full>

module attributes {llvm.dbg.cu = [#di_compile_unit]} {
  // CHECK-LABEL: @orphan_compile_unit
  llvm.func @orphan_compile_unit() {
    llvm.return
  }
}

// CHECK: !llvm.dbg.cu = !{![[CU:[0-9]+]]}
// CHECK: ![[CU]] = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14
// CHECK-SAME: producer: "clang"

// -----

// A unit named on the module *and* reached through a subprogram is translated
// once, so it appears in `!llvm.dbg.cu` once.

#di_file = #llvm.di_file<"t.c" in "/">
#di_compile_unit = #llvm.di_compile_unit<
  id = distinct[0]<>, sourceLanguage = DW_LANG_C99, file = #di_file,
  isOptimized = false, emissionKind = Full>
#di_subroutine_type = #llvm.di_subroutine_type<callingConvention = DW_CC_normal>
#di_subprogram = #llvm.di_subprogram<
  id = distinct[1]<>, compileUnit = #di_compile_unit, scope = #di_file,
  name = "reached", file = #di_file, subprogramFlags = Definition,
  type = #di_subroutine_type>

module attributes {llvm.dbg.cu = [#di_compile_unit]} {
  // CHECK-LABEL: @reached
  llvm.func @reached() {
    llvm.return
  } loc(fused<#di_subprogram>["t.c":1:1])
}

// The single entry is what this checks: the closing `}` immediately after the
// captured id means a second operand would not match. A `CHECK-NOT` on the
// following line would be dead, since FileCheck only scans past the match.
// CHECK: !llvm.dbg.cu = !{![[CU:[0-9]+]]}
// The one entry is the unit the subprogram reached, not a second copy of it.
// CHECK: ![[CU]] = distinct !DICompileUnit(language: DW_LANG_C99
// CHECK: !DISubprogram(name: "reached"
// CHECK-SAME: unit: ![[CU]]

// -----

// Every operation here carries an unknown location, so the location-driven
// debug paths emit nothing. The unit still comes out: the attribute exists to
// name a compile unit that nothing points at, which is exactly the case where
// no location leads to one.

#di_file = #llvm.di_file<"nowhere.c" in "/">
#di_compile_unit = #llvm.di_compile_unit<
  id = distinct[0]<>, sourceLanguage = DW_LANG_C99, file = #di_file,
  isOptimized = false, emissionKind = Full>

module attributes {llvm.dbg.cu = [#di_compile_unit]} {
  // CHECK-LABEL: @no_locations
  llvm.func @no_locations() {
    llvm.return
  }
}

// CHECK: !llvm.dbg.cu = !{![[UNKLOC:[0-9]+]]}
// And with the module flag, which a compile unit needs to survive being
// reloaded -- it is added whether or not any location drove debug emission,
// and checking it here keeps that true.
// CHECK: !{i32 2, !"Debug Info Version", i32 3}
// CHECK: ![[UNKLOC]] = distinct !DICompileUnit(language: DW_LANG_C99

// -----

// Without llvm.dbg.cu_in_order, a unit the module reaches comes first, before
// the units the attribute lists that nothing reaches; with it, the listed
// units come first in the attribute's order, whatever the module reaches.

#di_file_a = #llvm.di_file<"a.c" in "/">
#di_file_b = #llvm.di_file<"b.c" in "/">
#unit_a = #llvm.di_compile_unit<
  id = distinct[0]<>, sourceLanguage = DW_LANG_C99, file = #di_file_a,
  isOptimized = false, emissionKind = Full>
#unit_b = #llvm.di_compile_unit<
  id = distinct[1]<>, sourceLanguage = DW_LANG_C99, file = #di_file_b,
  isOptimized = false, emissionKind = Full>
#di_subroutine_type = #llvm.di_subroutine_type<callingConvention = DW_CC_normal>
#in_a = #llvm.di_subprogram<
  id = distinct[2]<>, compileUnit = #unit_a, scope = #di_file_a,
  name = "in_a", file = #di_file_a, subprogramFlags = Definition,
  type = #di_subroutine_type>

module attributes {llvm.dbg.cu = [#unit_b, #unit_a]} {
  // CHECK-LABEL: @reached_first
  llvm.func @reached_first() {
    llvm.return
  } loc(fused<#in_a>["a.c":1:1])
}

// CHECK: !llvm.dbg.cu = !{![[A:[0-9]+]], ![[B:[0-9]+]]}
// CHECK-DAG: ![[A]] = distinct !DICompileUnit({{.*}}file: ![[FA:[0-9]+]]
// CHECK-DAG: ![[FA]] = !DIFile(filename: "a.c"

// -----

#di_file_a = #llvm.di_file<"a.c" in "/">
#di_file_b = #llvm.di_file<"b.c" in "/">
#unit_a = #llvm.di_compile_unit<
  id = distinct[0]<>, sourceLanguage = DW_LANG_C99, file = #di_file_a,
  isOptimized = false, emissionKind = Full>
#unit_b = #llvm.di_compile_unit<
  id = distinct[1]<>, sourceLanguage = DW_LANG_C99, file = #di_file_b,
  isOptimized = false, emissionKind = Full>
#di_subroutine_type = #llvm.di_subroutine_type<callingConvention = DW_CC_normal>
#in_a = #llvm.di_subprogram<
  id = distinct[2]<>, compileUnit = #unit_a, scope = #di_file_a,
  name = "in_a", file = #di_file_a, subprogramFlags = Definition,
  type = #di_subroutine_type>

module attributes {llvm.dbg.cu = [#unit_b, #unit_a], llvm.dbg.cu_in_order} {
  // CHECK-LABEL: @listed_first
  llvm.func @listed_first() {
    llvm.return
  } loc(fused<#in_a>["a.c":1:1])
}

// CHECK: !llvm.dbg.cu = !{![[B:[0-9]+]], ![[A:[0-9]+]]}
// CHECK-DAG: ![[B]] = distinct !DICompileUnit({{.*}}file: ![[FB:[0-9]+]]
// CHECK-DAG: ![[FB]] = !DIFile(filename: "b.c"
