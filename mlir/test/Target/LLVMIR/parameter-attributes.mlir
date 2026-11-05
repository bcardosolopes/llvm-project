// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s
// RUN: mlir-opt %s -split-input-file --verify-roundtrip -o /dev/null

// The export mirror of Import/parameter-attributes.ll. Both directions run off
// the one AttrKind table, so a kind missing from it is lost coming and going.

// CHECK: define void @sret_return(ptr dead_on_unwind noalias writable sret(%struct.Big) align 8 %{{.*}})
llvm.func @sret_return(%arg0: !llvm.ptr {llvm.dead_on_unwind, llvm.noalias,
                                         llvm.writable,
                                         llvm.sret = !llvm.struct<"struct.Big", (f64)>,
                                         llvm.align = 8 : i64}) {
  llvm.return
}

// -----

// LLVM 21 removed the `nocapture` kind, so `llvm.nocapture` is the dialect's
// only spelling for it and it writes back out as `captures(none)`.
// CHECK: define void @captures_none(ptr captures(none) %{{.*}})
llvm.func @captures_none(%arg0: !llvm.ptr {llvm.nocapture}) {
  llvm.return
}

// -----

// CHECK: define float @nofpclass_param(float nofpclass(nan inf) %{{.*}})
llvm.func @nofpclass_param(%arg0: f32 {llvm.nofpclass = 519 : i64}) -> f32 {
  llvm.return %arg0 : f32
}

// -----

// CHECK: define void @initializes_param(ptr initializes((0, 4), (8, 12)) %{{.*}})
llvm.func @initializes_param(%arg0: !llvm.ptr {llvm.initializes = [
    #llvm.constant_range<i64, 0, 4>, #llvm.constant_range<i64, 8, 12>]}) {
  llvm.return
}

// -----

// CHECK: define void @noext_param(i32 noext %{{.*}})
llvm.func @noext_param(%arg0: i32 {llvm.noext}) {
  llvm.return
}

// -----

// CHECK: define void @swiftself_param(ptr swiftself %{{.*}})
llvm.func @swiftself_param(%arg0: !llvm.ptr {llvm.swiftself}) {
  llvm.return
}

// -----

// CHECK: define swiftcc void @swifterror_param(ptr swifterror %{{.*}})
llvm.func swiftcc @swifterror_param(%arg0: !llvm.ptr {llvm.swifterror}) {
  llvm.return
}

// -----

// CHECK: define swifttailcc void @swiftasync_param(ptr swiftasync %{{.*}})
llvm.func swifttailcc @swiftasync_param(%arg0: !llvm.ptr {llvm.swiftasync}) {
  llvm.return
}

// -----

// CHECK: define noext i32 @noext_result()
llvm.func @noext_result() -> (i32 {llvm.noext}) {
  %0 = llvm.mlir.constant(0 : i32) : i32
  llvm.return %0 : i32
}
