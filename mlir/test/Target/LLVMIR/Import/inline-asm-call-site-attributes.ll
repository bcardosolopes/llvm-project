; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP
; RUN: mlir-translate -import-llvm %s | mlir-opt -emit-bytecode -o %t.mlirbc
; RUN: mlir-translate -mlir-to-llvmir %t.mlirbc \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; The call-site attributes of an inline asm call: clang puts `nounwind` on
; every asm statement that is not `asm unwind`, and `memory(none)` or
; `memory(read)` on one without side effects. The import used to drop them
; along with every parameter attribute but `elementtype`.

; CHECK-LABEL: @clobber
define void @clobber() {
  ; CHECK: llvm.inline_asm has_side_effects
  ; CHECK-SAME: {no_unwind}
  ; ROUNDTRIP: call void asm sideeffect "", "~{memory}"() #[[NOUNWIND:[0-9]+]]
  call void asm sideeffect "", "~{memory}"() #0
  ret void
}

; CHECK-LABEL: @pure
define i32 @pure(i32 %x) {
  ; CHECK: llvm.inline_asm
  ; CHECK-SAME: operand_attrs = [{llvm.noundef}]
  ; CHECK-SAME: memory_effects = #llvm.memory_effects<other = none, argMem = none, inaccessibleMem = none, errnoMem = none, targetMem0 = none, targetMem1 = none>
  ; CHECK-SAME: no_unwind
  ; CHECK-SAME: res_attrs = [{llvm.noundef}]
  ; ROUNDTRIP: call noundef i32 asm "add $0, $1, #1", "=r,r"(i32 noundef %{{.*}}) #[[PURE:[0-9]+]]
  %y = call noundef i32 asm "add $0, $1, #1", "=r,r"(i32 noundef %x) #1
  ret i32 %y
}

; An indirect operand keeps its element type under the op's own key, beside
; its other parameter attributes. A void asm has no result in LLVM, and its
; argument 0 is still attribute index 1.
; CHECK-LABEL: @indirect
define i32 @indirect(ptr %p) {
  ; CHECK: llvm.inline_asm
  ; CHECK-SAME: operand_attrs = [{elementtype = i32}]
  ; CHECK-SAME: memory_effects = #llvm.memory_effects<other = read, argMem = read, inaccessibleMem = read, errnoMem = read, targetMem0 = read, targetMem1 = read>
  ; CHECK-SAME: no_unwind
  ; CHECK-SAME: passthrough = {{\[}}["foo", "bar"]]
  ; CHECK-SAME: will_return
  ; ROUNDTRIP: call i32 asm "ldr $0, $1", "=r,*m"(ptr elementtype(i32) %{{.*}}) #[[READ:[0-9]+]]
  %v = call i32 asm "ldr $0, $1", "=r,*m"(ptr elementtype(i32) %p) #2
  ; CHECK: llvm.inline_asm
  ; CHECK-SAME: operand_attrs = [{elementtype = i32, llvm.nonnull}]
  ; CHECK-SAME: {cold, convergent, no_merge, target_cpu = "generic"}
  ; ROUNDTRIP: call void asm "str wzr, $0", "=*m"(ptr nonnull elementtype(i32) %{{.*}}) #[[MISC:[0-9]+]]
  call void asm "str wzr, $0", "=*m"(ptr nonnull elementtype(i32) %p) #3
  ret i32 %v
}

; An asm call with no attributes gains none.
; CHECK-LABEL: @plain
define void @plain() {
  ; CHECK: llvm.inline_asm has_side_effects asm_dialect = att "nop", ""  : () -> !llvm.void
  ; ROUNDTRIP: call void asm sideeffect "nop", ""(){{$}}
  call void asm sideeffect "nop", ""()
  ret void
}

attributes #0 = { nounwind }
attributes #1 = { nounwind memory(none) }
attributes #2 = { nounwind willreturn memory(read) "foo"="bar" }
attributes #3 = { nomerge convergent cold "target-cpu"="generic" }

; ROUNDTRIP-DAG: attributes #[[NOUNWIND]] = { nounwind }
; ROUNDTRIP-DAG: attributes #[[PURE]] = { nounwind memory(none) }
; ROUNDTRIP-DAG: attributes #[[READ]] = { nounwind willreturn memory(read) "foo"="bar" }
; ROUNDTRIP-DAG: attributes #[[MISC]] = { cold convergent nomerge "target-cpu"="generic" }
