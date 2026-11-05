// RUN: mlir-opt %s --convert-func-to-llvm --split-input-file | FileCheck %s

// A parsed non-recursive struct is frozen, so it does not clash with the
// struct that the converter creates under the same name.
// CHECK: @create_clashes_on_conversion(!llvm.struct<"_Converted.foo", (i64)>)
func.func private @clashing_struct_name(!llvm.struct<"_Converted.foo", (f32)>)
func.func private @create_clashes_on_conversion(!llvm.struct<"foo", (index)>)

// -----

// CHECK: @merge_on_conversion(!llvm.struct<"_Converted.foo", (i64)>) attributes {sym_visibility = "private"}
func.func private @clashing_struct_name(!llvm.struct<"_Converted.foo", (i64)>)
func.func private @merge_on_conversion(!llvm.struct<"foo", (index)>)

// -----

// CHECK: @create_clashes_on_conversion_recursive(!llvm.struct<"foo", (!llvm.struct<"foo">, index)>)
func.func private @clashing_struct_name(!llvm.struct<"_Converted.foo", (struct<"_Converted.foo">, f32)>)
func.func private @create_clashes_on_conversion_recursive(!llvm.struct<"foo", (struct<"foo">, index)>)

// -----

// CHECK: @merge_on_conversion_recursive(!llvm.struct<"_Converted.foo", (struct<"_Converted.foo">, i64)>)
func.func private @clashing_struct_name(!llvm.struct<"_Converted.foo", (struct<"_Converted.foo">, i64)>)
func.func private @merge_on_conversion_recursive(!llvm.struct<"foo", (struct<"foo">, index)>)

// -----

// CHECK: @create_clashing_pack(!llvm.struct<"foo", packed (!llvm.struct<"foo">, index)>)
func.func private @clashing_struct_name(!llvm.struct<"_Converted.foo", (struct<"_Converted.foo">, i64)>)
func.func private @create_clashing_pack(!llvm.struct<"foo", packed (struct<"foo">, index)>)

// -----

// CHECK: @merge_on_conversion_pack(!llvm.struct<"_Converted.foo", packed (struct<"_Converted.foo">, i64)>)
func.func private @clashing_struct_name(!llvm.struct<"_Converted.foo", packed (struct<"_Converted.foo">, i64)>)
func.func private @merge_on_conversion_pack(!llvm.struct<"foo", packed (struct<"foo">, index)>)

// -----

// Parsed structs with the same name and different bodies convert to different
// structs, in either order.
// CHECK: @same_name_a(!llvm.struct<"_Converted.foo", (i64)>)
// CHECK: @same_name_b(!llvm.struct<"_Converted.foo", (i64, i64)>)
// CHECK: @same_name_packed(!llvm.struct<"_Converted.foo", packed (i64)>)
func.func private @same_name_a(!llvm.struct<"foo", (index)>)
func.func private @same_name_b(!llvm.struct<"foo", (index, index)>)
func.func private @same_name_packed(!llvm.struct<"foo", packed (index)>)

// -----

// CHECK: @same_name_d(!llvm.struct<"_Converted.foo", (i64, i64)>)
// CHECK: @same_name_c(!llvm.struct<"_Converted.foo", (i64)>)
func.func private @same_name_d(!llvm.struct<"foo", (index, index)>)
func.func private @same_name_c(!llvm.struct<"foo", (index)>)
