// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// CHECK-LABEL: define i32 @asm_goto(
llvm.func @asm_goto(%arg0: i32) -> i32 {
  %0 = llvm.mlir.constant(7 : i32) : i32
  // CHECK: %[[RES:.*]] = callbr i32 asm sideeffect inteldialect "mov $0, $1; jmp ${2:l}; jmp ${3:l}", "=r,r,!i,!i"(i32 %{{.*}}) #[[$ATTRS:[0-9]+]]
  // CHECK-NEXT: to label %[[FALL:.*]] [label %[[BOTH:.*]], label %[[RET:.*]]], !srcloc ![[$SRCLOC:[0-9]+]]
  %1 = llvm.callbr has_side_effects asm_dialect = intel "mov $0, $1; jmp ${2:l}; jmp ${3:l}", "=r,r,!i,!i"(%arg0) : (i32) -> i32 to ^bb1(%0 : i32) [^bb2(%0, %arg0 : i32, i32), ^bb3] {no_unwind, memory_effects = #llvm.memory_effects<other = read, argMem = none, inaccessibleMem = none, errnoMem = none, targetMem0 = none, targetMem1 = none>, passthrough = ["nobuiltin"], srcloc = array<i64: 5>}
^bb1(%2: i32):
  // CHECK: [[FALL]]:
  // CHECK-NEXT: phi i32 [ 7, %{{.*}} ]
  %3 = llvm.add %1, %2 : i32
  llvm.return %3 : i32
^bb2(%4: i32, %5: i32):
  // CHECK: [[BOTH]]:
  // CHECK-NEXT: phi i32 [ 7, %{{.*}} ]
  // CHECK-NEXT: phi i32 [ %{{.*}}, %{{.*}} ]
  %6 = llvm.add %4, %5 : i32
  llvm.return %6 : i32
^bb3:
  // CHECK: [[RET]]:
  // CHECK-NEXT: ret i32 %[[RES]]
  llvm.return %1 : i32
}


// An indirect operand's element type is an argument attribute, also when the
// asm has no result.
// CHECK-LABEL: define void @void_asm_operand_attrs(
llvm.func @void_asm_operand_attrs(%arg0: !llvm.ptr) {
  // CHECK: callbr void asm sideeffect "incl $0; jo ${1:l}", "=*m,!i"(ptr elementtype(i32) %{{.*}})
  llvm.callbr has_side_effects operand_attrs = [{elementtype = i32}] "incl $0; jo ${1:l}", "=*m,!i"(%arg0) : (!llvm.ptr) -> () to ^bb1 [^bb2]
^bb1:
  llvm.return
^bb2:
  llvm.return
}

// The same for `llvm.inline_asm` built with no result.
// CHECK-LABEL: define void @inline_asm_no_result(
llvm.func @inline_asm_no_result(%arg0: !llvm.ptr) {
  // CHECK: call void asm sideeffect "incl $0", "=*m"(ptr elementtype(i32) %{{.*}})
  llvm.inline_asm has_side_effects operand_attrs = [{elementtype = i32}] "incl $0", "=*m" %arg0 : (!llvm.ptr) -> ()
  llvm.return
}

// CHECK: attributes #[[$ATTRS]] = { nobuiltin nounwind memory(read, argmem: none, inaccessiblemem: none, errnomem: none, target_mem0: none, target_mem1: none) }
// CHECK: ![[$SRCLOC]] = !{i64 5}
