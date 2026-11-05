// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.mlir.global external constant @vt() : !llvm.array<4 x ptr> {
  %0 = llvm.mlir.zero : !llvm.ptr
  %1 = llvm.mlir.undef : !llvm.array<4 x ptr>
  %2 = llvm.insertvalue %0, %1[0] : !llvm.array<4 x ptr>
  %3 = llvm.insertvalue %0, %2[1] : !llvm.array<4 x ptr>
  %4 = llvm.insertvalue %0, %3[2] : !llvm.array<4 x ptr>
  %5 = llvm.insertvalue %0, %4[3] : !llvm.array<4 x ptr>
  llvm.return %5 : !llvm.array<4 x ptr>
}

// CHECK: @user = global ptr getelementptr inbounds inrange(-16, 16) (i8, ptr @vt, i64 16)
llvm.mlir.global external @user() : !llvm.ptr {
  %0 = llvm.mlir.addressof @vt : !llvm.ptr
  %1 = llvm.mlir.constant(16 : i64) : i64
  %2 = llvm.getelementptr inrange(-16, 16) inbounds %0[%1] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  llvm.return %2 : !llvm.ptr
}

// CHECK-LABEL: define void @ctor
// CHECK: store ptr getelementptr inbounds nuw inrange(-16, 16) (i8, ptr @vt, i64 16), ptr %0
// CHECK: %[[GEP:.*]] = getelementptr inbounds i8, ptr %0, i64 %1
// CHECK: store ptr %[[GEP]], ptr %0
// CHECK: %[[VTGEP:.*]] = getelementptr inbounds i8, ptr @vt, i64 %1
// CHECK: store ptr %[[VTGEP]], ptr %0
llvm.func @ctor(%this: !llvm.ptr, %offset: i64) {
  %0 = llvm.mlir.addressof @vt : !llvm.ptr
  %1 = llvm.mlir.constant(16 : i64) : i64
  %2 = llvm.getelementptr inrange(-16, 16) inbounds|nuw %0[%1] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  llvm.store %2, %this : !llvm.ptr, !llvm.ptr
  %3 = llvm.getelementptr inrange(-16, 16) inbounds %this[%offset] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  llvm.store %3, %this : !llvm.ptr, !llvm.ptr
  %4 = llvm.getelementptr inrange(-16, 16) inbounds %0[%offset] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  llvm.store %4, %this : !llvm.ptr, !llvm.ptr
  llvm.return
}
