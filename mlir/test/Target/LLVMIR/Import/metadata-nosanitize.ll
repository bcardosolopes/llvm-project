; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; !nosanitize suppresses sanitizer instrumentation of an access. All four
; sanitizers read it -- Address, HWAddress, Memory and Thread -- so losing it
; means instrumenting accesses the compiler deliberately marked as its own.

; LLVM permits the metadata on *any* instruction, so there is no single carrier
; op to hang a typed attribute off. It rides as a discardable dialect attribute,
; the way llvm.type_metadata already does.

; CHECK-NOT: dropped metadata

declare ptr @f()

; CHECK-LABEL: llvm.func @nosan
define void @nosan(ptr %p) {
  ; CHECK: llvm.load %{{.*}} {alignment = 4 : i64, llvm.nosanitize}
  %a = load i32, ptr %p, !nosanitize !0
  ; CHECK: llvm.store %{{.*}} {alignment = 4 : i64, llvm.nosanitize}
  store i32 %a, ptr %p, !nosanitize !0
  ; CHECK: llvm.call @f() {llvm.nosanitize}
  %c = call ptr @f(), !nosanitize !0
  ; CHECK: llvm.add %{{.*}} {llvm.nosanitize}
  %d = add i32 %a, 1, !nosanitize !0
  ; CHECK: llvm.getelementptr %{{.*}} {llvm.nosanitize}
  %e = getelementptr i8, ptr %p, i32 4, !nosanitize !0
  ret void
}

; An unmarked access must stay unmarked.
; CHECK-LABEL: llvm.func @plain
; CHECK: llvm.load
; CHECK-NOT: llvm.nosanitize
define i32 @plain(ptr %p) {
  %a = load i32, ptr %p
  ret i32 %a
}

!0 = !{}
