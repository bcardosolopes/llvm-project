; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

@vt = constant { [3 x ptr], [2 x ptr] } { [3 x ptr] [ptr null, ptr null, ptr @ctor], [2 x ptr] [ptr null, ptr @ctor] }

; CHECK-LABEL: llvm.mlir.global external @user()
; CHECK: llvm.getelementptr inrange(-16, 8) inbounds %{{.*}}[%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i8
; ROUNDTRIP: @user = global ptr getelementptr inbounds inrange(-16, 8) (i8, ptr @vt, i64 16)
@user = global ptr getelementptr inbounds inrange(-16, 8) (i8, ptr @vt, i64 16)

; CHECK-LABEL: llvm.func @ctor
; CHECK: llvm.getelementptr inrange(-16, 8) inbounds|nuw %{{.*}}[%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i8
; CHECK: llvm.getelementptr inrange(-8, 8) inbounds %{{.*}}[%{{.*}}, 1, %{{.*}}] : (!llvm.ptr, i32, i32) -> !llvm.ptr, !llvm.struct<(array<3 x ptr>, array<2 x ptr>)>
; CHECK: llvm.getelementptr inbounds %{{.*}}[%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i8
; ROUNDTRIP-LABEL: define void @ctor
; ROUNDTRIP: store ptr getelementptr inbounds nuw inrange(-16, 8) (i8, ptr @vt, i64 16)
; ROUNDTRIP: store ptr getelementptr inbounds inrange(-8, 8) ({ [3 x ptr], [2 x ptr] }, ptr @vt, i32 0, i32 1, i32 1)
; ROUNDTRIP: store ptr getelementptr inbounds {{(nuw )?}}(i8, ptr @vt, i64 8)
define void @ctor(ptr %this) {
  store ptr getelementptr inbounds nuw inrange(-16, 8) (i8, ptr @vt, i64 16), ptr %this
  store ptr getelementptr inbounds inrange(-8, 8) ({ [3 x ptr], [2 x ptr] }, ptr @vt, i32 0, i32 1, i32 1), ptr %this
  store ptr getelementptr inbounds (i8, ptr @vt, i64 8), ptr %this
  ret void
}
