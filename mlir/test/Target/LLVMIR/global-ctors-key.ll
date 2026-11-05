; A constructor or destructor keyed to a global keeps its key through the
; round trip. Code generation puts a keyed entry in its key's comdat group, so
; the entry is dropped with the group when the linker discards it; without the
; key, a discarded inline variable's initializer would still run.

; RUN: mlir-translate -import-llvm %s | FileCheck --check-prefix=IMPORT %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s

$var = comdat any

@var = linkonce_odr global i32 0, comdat
@guard = linkonce_odr global i64 0, comdat($var)

; IMPORT: llvm.mlir.global_ctors ctors = [@init, @plain], priorities = [65535 : i32, 101 : i32], data = [@var, #llvm.zero]
; IMPORT: llvm.mlir.global_dtors dtors = [@fini], priorities = [65535 : i32], data = [@var]
; CHECK: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init, ptr @var }, { i32, ptr, ptr } { i32 101, ptr @plain, ptr null }]
; CHECK: @llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @fini, ptr @var }]
@llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init, ptr @var }, { i32, ptr, ptr } { i32 101, ptr @plain, ptr null }]
@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @fini, ptr @var }]

define internal void @init() comdat($var) {
  store i32 1, ptr @var
  ret void
}

define internal void @plain() {
  ret void
}

define internal void @fini() comdat($var) {
  ret void
}
