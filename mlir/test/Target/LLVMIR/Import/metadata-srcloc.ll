; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; `!srcloc` carries the source-location cookies clang attaches to inline asm, so
; backend diagnostics about the asm can be traced back to what produced it. It
; is not a fixed metadata kind -- it is registered by name.

; CHECK-NOT: dropped metadata
; CHECK-LABEL: @asm_srcloc
define i32 @asm_srcloc(i32 %x) {
  ; A multi-line asm string gets one cookie per line, so the attribute is an
  ; array rather than a single value.
  ; CHECK: llvm.inline_asm
  ; CHECK-SAME: srcloc = array<i64: 93, 187>
  %r = call i32 asm sideeffect "mov $0, $1", "=r,r"(i32 %x), !srcloc !0
  ret i32 %r
}

; A null operand is legal in an MDNode -- RAUW leaves them behind -- so the read
; has to tolerate one rather than assert. The node is declined whole: a cookie
; list with a hole in it would misattribute the remaining lines of the asm.

; CHECK-LABEL: llvm.func @srcloc_with_null
; CHECK-NOT: srcloc
define void @srcloc_with_null() {
  call void asm sideeffect "nop", ""(), !srcloc !99
  ret void
}

!0 = !{i64 93, i64 187}
!99 = !{i64 42, null}
