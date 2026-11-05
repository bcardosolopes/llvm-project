; An intrinsic call's attributes survive the import: the call-site function
; attributes (`minsize`, `nounwind`, `noreturn`, string attributes) clang
; writes on the calls it emits ride as `llvm.call_fn_attrs`, and the parameter
; attributes ride as `llvm.call_arg_attrs` and `llvm.call_res_attrs`, unless
; the op models them in a typed slot (as the memory intrinsics do with
; `align` and `noundef`, which their lowering reads).

; RUN: mlir-translate -import-llvm %s | FileCheck --check-prefix=IMPORT %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s

; Fixed point: pass 1 and pass 2 must be byte-identical.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir > %t.pass1.ll
; RUN: mlir-translate -import-llvm %t.pass1.ll | mlir-translate -mlir-to-llvmir > %t.pass2.ll
; RUN: diff %t.pass1.ll %t.pass2.ll

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.lifetime.start.p0(ptr)
declare void @llvm.trap()
declare i64 @llvm.abs.i64(i64, i1)

; IMPORT-LABEL: llvm.func @intrinsics(
; IMPORT: llvm.intr.lifetime.start %{{.*}} {llvm.call_arg_attrs = [{llvm.nonnull}], llvm.call_fn_attrs = ["nounwind"]}
; IMPORT: "llvm.intr.memcpy"(%{{.*}}) <{arg_attrs = [{llvm.align = 8 : i64, llvm.noundef}, {llvm.align = 4 : i64, llvm.noundef}, {llvm.noundef, llvm.range = #llvm.constant_range<i64, 0, 100>}], isVolatile = false}> {llvm.call_fn_attrs = ["minsize", "nounwind", "optsize", ["no-builtins", ""]]}
; IMPORT: llvm.intr.abs{{.*}}llvm.call_res_attrs = {llvm.noundef}
; IMPORT: llvm.intr.trap {llvm.call_fn_attrs = ["noreturn", "nounwind"]

; CHECK-LABEL: define i64 @intrinsics(
; CHECK: call void @llvm.lifetime.start.p0(ptr nonnull %{{.*}}) #[[$NOUNWIND:[0-9]+]]
; CHECK: call void @llvm.memcpy.p0.p0.i64(ptr noundef align 8 %{{.*}}, ptr noundef align 4 %{{.*}}, i64 noundef range(i64 0, 100) %{{.*}}, i1 false) #[[$SIZE:[0-9]+]]
; CHECK: call noundef i64 @llvm.abs.i64(
; CHECK: tail call void @llvm.trap() #[[$NORETURN:[0-9]+]]
; CHECK-DAG: attributes #[[$NOUNWIND]] = { nounwind }
; CHECK-DAG: attributes #[[$SIZE]] = { minsize nounwind optsize "no-builtins" }
; CHECK-DAG: attributes #[[$NORETURN]] = { noreturn nounwind }
define i64 @intrinsics(ptr %d, ptr %s, i64 %n) {
  %a = alloca [24 x i8], align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %a) #0
  call void @llvm.memcpy.p0.p0.i64(ptr noundef align 8 %d, ptr noundef align 4 %s, i64 noundef range(i64 0, 100) %n, i1 noundef false) #1
  %r = call noundef i64 @llvm.abs.i64(i64 %n, i1 false)
  %c = icmp eq i64 %r, 0
  br i1 %c, label %t, label %f
t:
  tail call void @llvm.trap() #2
  unreachable
f:
  ret i64 %r
}

attributes #0 = { nounwind }
attributes #1 = { minsize nounwind optsize "no-builtins" }
attributes #2 = { noreturn nounwind }
