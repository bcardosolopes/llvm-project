; RUN: mlir-translate -import-llvm -emit-expensive-warnings -split-input-file %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s
; The warning is behind `-emit-expensive-warnings`, so without the flag there
; must be nothing at all -- checked over the whole output rather than from a
; match onwards, as the sibling `dropped-metadata.ll` tests do.
; RUN: mlir-translate -import-llvm -split-input-file %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=QUIET --allow-empty

; A phi becomes a block argument, and a block argument has nowhere to carry
; fast-math flags: `Block` has no attribute dictionary, and `arg_attrs` only
; reaches a function's entry block. `phi fast float` therefore comes back as a
; plain `phi float`.
;
; Representing it needs a change to how the dialect models phis. Until then the
; loss is at least reported -- silence is what let this class of loss hide.

; A lone `-NOT` with no preceding match scans the whole output, which is the
; point: with the flag off there must be no warning anywhere, not merely none
; after some earlier match.
; QUIET-NOT: dropped fast-math flags

; CHECK: warning: dropped fast-math flags: a phi becomes a block argument, which cannot carry them: {{.*}}phi fast float
; ...and exactly one. The `-NOT` runs from that match to the end of the output,
; which is where the flagless phi of the second split lands, so a regression
; that warned on every phi would fail here.
; CHECK-NOT: dropped fast-math flags
define float @phi_with_flags(i1 %c, float %x, float %y) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %m
b:
  br label %m
m:
  %p = phi fast float [ %x, %a ], [ %y, %b ]
  ret float %p
}

; // -----

; A phi with no flags loses nothing, so it must stay quiet -- otherwise the
; warning is noise on every phi in the module. The `CHECK-NOT` above is what
; covers this, on the run that *has* the flag; `QUIET-NOT` only covers the run
; without it.
define float @phi_without_flags(i1 %c, float %x, float %y) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %m
b:
  br label %m
m:
  %p = phi float [ %x, %a ], [ %y, %b ]
  ret float %p
}
