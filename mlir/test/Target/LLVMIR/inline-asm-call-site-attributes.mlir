// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s
// RUN: mlir-opt -split-input-file %s --verify-roundtrip -o /dev/null

// The call-site function, result and parameter attributes of `llvm.inline_asm`
// are written back onto the call of its asm.

// CHECK-LABEL: define i32 @attrs(
llvm.func @attrs(%x: i32, %p: !llvm.ptr) -> i32 {
  // CHECK: call void asm sideeffect "", "~{memory}"() #[[NOUNWIND:[0-9]+]]
  llvm.inline_asm has_side_effects {no_unwind} "", "~{memory}" : () -> ()
  // CHECK: call noundef i32 asm "add $0, $1, #1", "=r,r"(i32 noundef %{{.*}}) #[[PURE:[0-9]+]]
  %0 = llvm.inline_asm operand_attrs = [{llvm.noundef}] {memory_effects = #llvm.memory_effects<other = none, argMem = none, inaccessibleMem = none, errnoMem = none, targetMem0 = none, targetMem1 = none>, no_unwind, res_attrs = [{llvm.noundef}]} "add $0, $1, #1", "=r,r" %x : (i32) -> i32
  // CHECK: call i32 asm "ldr $0, $1", "=r,*m"(ptr elementtype(i32) %{{.*}}) #[[READ:[0-9]+]]
  %1 = llvm.inline_asm operand_attrs = [{elementtype = i32}] {memory_effects = #llvm.memory_effects<other = read, argMem = read, inaccessibleMem = read, errnoMem = read, targetMem0 = read, targetMem1 = read>, no_unwind, passthrough = [["foo", "bar"]], will_return} "ldr $0, $1", "=r,*m" %p : (!llvm.ptr) -> i32
  llvm.return %0 : i32
}

// CHECK-DAG: attributes #[[NOUNWIND]] = { nounwind }
// CHECK-DAG: attributes #[[PURE]] = { nounwind memory(none) }
// CHECK-DAG: attributes #[[READ]] = { nounwind willreturn memory(read) "foo"="bar" }

// -----

// An asm with no result: its operand attributes are still indexed by argument,
// so the element type lands on argument 0 and not on the return value.

// CHECK-LABEL: define void @no_result(
llvm.func @no_result(%p: !llvm.ptr) {
  // CHECK: call void asm "str wzr, $0", "=*m"(ptr nonnull elementtype(i32) %{{.*}}) #[[MISC:[0-9]+]]
  llvm.inline_asm operand_attrs = [{elementtype = i32, llvm.nonnull}] {cold, convergent, no_merge, target_cpu = "generic"} "str wzr, $0", "=*m" %p : (!llvm.ptr) -> ()
  llvm.return
}

// CHECK: attributes #[[MISC]] = { cold convergent nomerge "target-cpu"="generic" }
