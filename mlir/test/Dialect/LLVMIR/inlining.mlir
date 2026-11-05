// RUN: mlir-opt %s -inline -split-input-file | FileCheck %s

#file = #llvm.di_file<"foo.mlir" in "/foo/">
#variable = #llvm.di_local_variable<scope = #file>
#variableAddr = #llvm.di_local_variable<scope = #file>
#label = #llvm.di_label<scope = #file>

func.func @inner_func_inlinable(%ptr : !llvm.ptr) -> i32 {
  %0 = llvm.mlir.constant(42 : i32) : i32
  %stack = llvm.intr.stacksave : !llvm.ptr
  llvm.store %0, %ptr { alignment = 8 } : i32, !llvm.ptr
  %1 = llvm.load %ptr { alignment = 8 } : !llvm.ptr -> i32
  llvm.intr.dbg.value #variable = %0 : i32
  llvm.intr.dbg.declare #variableAddr = %ptr : !llvm.ptr
  llvm.intr.dbg.label #label
  %byte = llvm.mlir.constant(43 : i8) : i8
  %true = llvm.mlir.constant(1 : i1) : i1
  "llvm.intr.memset"(%ptr, %byte, %0) <{isVolatile = true}> : (!llvm.ptr, i8, i32) -> ()
  "llvm.intr.memmove"(%ptr, %ptr, %0) <{isVolatile = true}> : (!llvm.ptr, !llvm.ptr, i32) -> ()
  "llvm.intr.memcpy"(%ptr, %ptr, %0) <{isVolatile = true}> : (!llvm.ptr, !llvm.ptr, i32) -> ()
  llvm.intr.assume %true : i1
  llvm.fence release
  %2 = llvm.atomicrmw add %ptr, %0 monotonic : !llvm.ptr, i32
  %3 = llvm.cmpxchg %ptr, %0, %1 acq_rel monotonic : !llvm.ptr, i32
  llvm.inline_asm has_side_effects "foo", "bar" : () -> ()
  llvm.cond_br %true, ^bb1, ^bb2
^bb1:
  llvm.unreachable
^bb2:
  llvm.intr.stackrestore %stack : !llvm.ptr
  llvm.call_intrinsic "llvm.x86.sse41.round.ss"() : () -> (vector<8xf32>)
  return %1 : i32
}

// CHECK-LABEL: func.func @test_inline(
// CHECK-SAME: %[[PTR:[a-zA-Z0-9_]+]]
// CHECK: %[[CST:.*]] = llvm.mlir.constant(42
// CHECK: %[[STACK:.+]] = llvm.intr.stacksave
// CHECK: llvm.store %[[CST]], %[[PTR]]
// CHECK: %[[RES:.+]] = llvm.load %[[PTR]]
// CHECK: llvm.intr.dbg.value #{{.+}} = %[[CST]]
// CHECK: llvm.intr.dbg.declare #{{.+}} = %[[PTR]]
// CHECK: llvm.intr.dbg.label #{{.+}}
// CHECK: "llvm.intr.memset"(%[[PTR]]
// CHECK: "llvm.intr.memmove"(%[[PTR]], %[[PTR]]
// CHECK: "llvm.intr.memcpy"(%[[PTR]], %[[PTR]]
// CHECK: llvm.intr.assume
// CHECK: llvm.fence release
// CHECK: llvm.atomicrmw add %[[PTR]], %[[CST]] monotonic
// CHECK: llvm.cmpxchg %[[PTR]], %[[CST]], %[[RES]] acq_rel monotonic
// CHECK: llvm.inline_asm has_side_effects "foo", "bar"
// CHECK: llvm.unreachable
// CHECK: llvm.intr.stackrestore %[[STACK]]
// CHECK: llvm.call_intrinsic "llvm.x86.sse41.round.ss"(
func.func @test_inline(%ptr : !llvm.ptr) -> i32 {
  %0 = call @inner_func_inlinable(%ptr) : (!llvm.ptr) -> i32
  return %0 : i32
}

// -----
// Check that llvm.return is correctly handled

func.func @func(%arg0 : i32) -> i32  {
  llvm.return %arg0 : i32
}
// CHECK-LABEL: @llvm_ret
// CHECK-NOT: call
// CHECK:  return %arg0
func.func @llvm_ret(%arg0 : i32) -> i32 {
  %res = call @func(%arg0) : (i32) -> (i32)
  return %res : i32
}

// -----

// Include all function attributes that don't prevent inlining
llvm.func internal fastcc @callee() -> (i32) attributes { function_entry_count = 42 : i64, dso_local } {
  %0 = llvm.mlir.constant(42 : i32) : i32
  llvm.return %0 : i32
}

// CHECK-LABEL: llvm.func @caller
// CHECK-NEXT: %[[CST:.+]] = llvm.mlir.constant
// CHECK-NEXT: llvm.return %[[CST]]
llvm.func @caller() -> (i32) {
  // Include all call attributes that don't prevent inlining.
  %0 = llvm.call fastcc @callee() { fastmathFlags = #llvm.fastmath<nnan, ninf>, branch_weights = dense<42> : vector<1xi32> } : () -> (i32)
  llvm.return %0 : i32
}

// -----

llvm.func @foo() -> (i32) attributes { no_inline } {
  %0 = llvm.mlir.constant(0 : i32) : i32
  llvm.return %0 : i32
}

llvm.func @bar() -> (i32) {
  %0 = llvm.mlir.constant(1 : i32) : i32
  llvm.return %0 : i32
}

llvm.func @callee_with_multiple_blocks(%cond: i1) -> (i32) {
  llvm.cond_br %cond, ^bb1, ^bb2
^bb1:
  %0 = llvm.call @foo() : () -> (i32)
  llvm.br ^bb3(%0: i32)
^bb2:
  %1 = llvm.call @bar() { no_inline } : () -> (i32)
  llvm.br ^bb3(%1: i32)
^bb3(%arg: i32):
  llvm.return %arg : i32
}

// CHECK-LABEL: llvm.func @caller
// CHECK-NEXT: llvm.cond_br {{.+}}, ^[[BB1:.+]], ^[[BB2:.+]]
// CHECK-NEXT: ^[[BB1]]:
// CHECK-NEXT: llvm.call @foo
// CHECK-NEXT: llvm.br ^[[BB3:[a-zA-Z0-9_]+]]
// CHECK-NEXT: ^[[BB2]]:
// CHECK-NEXT: llvm.call @bar
// CHECK-NEXT: llvm.br ^[[BB3]]
// CHECK-NEXT: ^[[BB3]]
// CHECK-NEXT: llvm.br ^[[BB4:[a-zA-Z0-9_]+]]
// CHECK-NEXT: ^[[BB4]]
// CHECK-NEXT: llvm.return
llvm.func @caller(%cond: i1) -> (i32) {
  %0 = llvm.call @callee_with_multiple_blocks(%cond) : (i1) -> (i32)
  llvm.return %0 : i32
}

// -----

llvm.func @personality() -> i32

llvm.func @callee() -> (i32) attributes { personality = @personality } {
  %0 = llvm.mlir.constant(42 : i32) : i32
  llvm.return %0 : i32
}

// A caller without a personality adopts the callee's, as in
// `llvm::InlineFunction`.
// CHECK-LABEL: llvm.func @caller
// CHECK-SAME: personality = @personality
// CHECK-NOT: llvm.call @callee
// CHECK: %[[CST:.+]] = llvm.mlir.constant(42
// CHECK: llvm.return %[[CST]]
llvm.func @caller() -> (i32) {
  %0 = llvm.call @callee() : () -> (i32)
  llvm.return %0 : i32
}

// -----

llvm.func @personality() -> i32

llvm.func @callee() -> (i32) attributes { personality = @personality } {
  %0 = llvm.mlir.constant(42 : i32) : i32
  llvm.return %0 : i32
}

// Matching personalities inline and leave the caller's attribute untouched.
// CHECK-LABEL: llvm.func @caller
// CHECK-SAME: personality = @personality
// CHECK-NOT: llvm.call @callee
// CHECK: %[[CST:.+]] = llvm.mlir.constant(42
// CHECK: llvm.return %[[CST]]
llvm.func @caller() -> (i32) attributes { personality = @personality } {
  %0 = llvm.call @callee() : () -> (i32)
  llvm.return %0 : i32
}

// -----

llvm.func @personality() -> i32
llvm.func @other_personality() -> i32

llvm.func @callee() -> (i32) attributes { personality = @personality } {
  %0 = llvm.mlir.constant(42 : i32) : i32
  llvm.return %0 : i32
}

// Conflicting personalities still block inlining, matching LLVM's
// "incompatible personality" bail-out.
// CHECK-LABEL: llvm.func @caller
// CHECK-SAME: personality = @other_personality
// CHECK-NEXT: llvm.call @callee
// CHECK-NEXT: llvm.return
llvm.func @caller() -> (i32) attributes { personality = @other_personality } {
  %0 = llvm.call @callee() : () -> (i32)
  llvm.return %0 : i32
}

// -----

llvm.func @personality() -> i32
llvm.mlir.global external constant @_ZTIi() : !llvm.ptr
llvm.func @may_throw()

// A callee whose body carries landing pads is inlinable too; the personality
// propagated onto the caller is what keeps `llvm.landingpad` verifiable.
llvm.func @callee() -> (i32) attributes { personality = @personality } {
  %0 = llvm.mlir.constant(42 : i32) : i32
  llvm.invoke @may_throw() to ^bb1 unwind ^bb2 : () -> ()
^bb1:
  llvm.return %0 : i32
^bb2:
  %1 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  llvm.resume %1 : !llvm.struct<(ptr, i32)>
}

// CHECK-LABEL: llvm.func @caller
// CHECK-SAME: personality = @personality
// CHECK-NOT: llvm.call @callee
// CHECK: llvm.invoke @may_throw
// CHECK: llvm.landingpad
// CHECK: llvm.resume
llvm.func @caller() -> (i32) {
  %0 = llvm.call @callee() : () -> (i32)
  llvm.return %0 : i32
}

// -----

llvm.func @callee() attributes { passthrough = ["foo", "bar"] } {
  llvm.return
}

// CHECK-LABEL: llvm.func @caller
// CHECK-NEXT: llvm.return
llvm.func @caller() {
  llvm.call @callee() : () -> ()
  llvm.return
}

// -----

llvm.func @callee_noinline() attributes { no_inline } {
  llvm.return
}

llvm.func @callee_presplitcoroutine() attributes { passthrough = ["presplitcoroutine"] } {
  llvm.return
}

llvm.func @callee_strictfp() attributes { passthrough = ["strictfp"] } {
  llvm.return
}

// CHECK-LABEL: llvm.func @caller
// CHECK-NEXT: llvm.call @callee_noinline
// CHECK-NEXT: llvm.call @callee_presplitcoroutine
// CHECK-NEXT: llvm.call @callee_strictfp
// CHECK-NEXT: llvm.return
llvm.func @caller() {
  llvm.call @callee_noinline() : () -> ()
  llvm.call @callee_presplitcoroutine() : () -> ()
  llvm.call @callee_strictfp() : () -> ()
  llvm.return
}

// -----

llvm.func @static_alloca() -> f32 {
  %0 = llvm.mlir.constant(4 : i32) : i32
  %1 = llvm.alloca %0 x f32 : (i32) -> !llvm.ptr
  %2 = llvm.load %1 : !llvm.ptr -> f32
  llvm.return %2 : f32
}

llvm.func @dynamic_alloca(%size : i32) -> f32 {
  %0 = llvm.add %size, %size : i32
  %1 = llvm.alloca %0 x f32 : (i32) -> !llvm.ptr
  %2 = llvm.load %1 : !llvm.ptr -> f32
  llvm.return %2 : f32
}

// CHECK-LABEL: llvm.func @test_inline
llvm.func @test_inline(%cond : i1, %size : i32) -> f32 {
  // Check that the static alloca was moved to the entry block after inlining
  // with its size defined by a constant.
  // CHECK-NOT: ^{{.+}}:
  // CHECK-NEXT: llvm.mlir.constant
  // CHECK-NEXT: llvm.alloca
  // CHECK: llvm.cond_br
  llvm.cond_br %cond, ^bb1, ^bb2
  // CHECK: ^{{.+}}:
^bb1:
  // CHECK-NOT: llvm.call @static_alloca
  // CHECK: llvm.intr.lifetime.start
  %0 = llvm.call @static_alloca() : () -> f32
  // CHECK: llvm.intr.lifetime.end
  // CHECK: llvm.br ^[[BB3:[a-zA-Z0-9_]+]]
  llvm.br ^bb3(%0: f32)
  // CHECK: ^{{.+}}:
^bb2:
  // Check that the dynamic alloca was inlined, but that it was not moved to the
  // entry block.
  // CHECK: %[[STACK:[a-zA-Z0-9_]+]] = llvm.intr.stacksave
  // CHECK: llvm.add
  // CHECK: llvm.alloca
  // CHECK: llvm.intr.stackrestore %[[STACK]]
  // CHECK-NOT: llvm.call @dynamic_alloca
  %1 = llvm.call @dynamic_alloca(%size) : (i32) -> f32
  // CHECK: llvm.br ^[[BB3]]
  llvm.br ^bb3(%1: f32)
  // CHECK: ^[[BB3]]
^bb3(%arg : f32):
  // CHECK-NEXT: return
  llvm.return %arg : f32
}

// -----

llvm.func @static_alloca_not_in_entry(%cond : i1) -> f32 {
  llvm.cond_br %cond, ^bb1, ^bb2
^bb1:
  %0 = llvm.mlir.constant(4 : i32) : i32
  %1 = llvm.alloca %0 x f32 : (i32) -> !llvm.ptr
  llvm.br ^bb3(%1: !llvm.ptr)
^bb2:
  %2 = llvm.mlir.constant(8 : i32) : i32
  %3 = llvm.alloca %2 x f32 : (i32) -> !llvm.ptr
  llvm.br ^bb3(%3: !llvm.ptr)
^bb3(%ptr : !llvm.ptr):
  %4 = llvm.load %ptr : !llvm.ptr -> f32
  llvm.return %4 : f32
}

// CHECK-LABEL: llvm.func @test_inline
llvm.func @test_inline(%cond : i1) -> f32 {
  // Make sure the alloca was not moved to the entry block.
  // CHECK-NOT: llvm.alloca
  // CHECK: llvm.cond_br
  // CHECK: llvm.alloca
  %0 = llvm.call @static_alloca_not_in_entry(%cond) : (i1) -> f32
  llvm.return %0 : f32
}

// -----

llvm.func @static_alloca(%cond: i1) -> f32 {
  %0 = llvm.mlir.constant(4 : i32) : i32
  %1 = llvm.alloca %0 x f32 : (i32) -> !llvm.ptr
  llvm.cond_br %cond, ^bb1, ^bb2
^bb1:
  %2 = llvm.load %1 : !llvm.ptr -> f32
  llvm.return %2 : f32
^bb2:
  %3 = llvm.mlir.constant(3.14192 : f32) : f32
  llvm.return %3 : f32
}

// CHECK-LABEL: llvm.func @test_inline
llvm.func @test_inline(%cond0 : i1, %cond1 : i1, %funcArg : f32) -> f32 {
  // CHECK-NOT: llvm.cond_br
  // CHECK: %[[PTR:.+]] = llvm.alloca
  // CHECK: llvm.cond_br %{{.+}}, ^[[BB1:.+]], ^{{.+}}
  llvm.cond_br %cond0, ^bb1, ^bb2
  // CHECK: ^[[BB1]]
^bb1:
  // Make sure the lifetime begin intrinsic has been inserted where the call
  // used to be, even though the alloca has been moved to the entry block.
  // CHECK-NEXT: llvm.intr.lifetime.start %[[PTR]]
  %0 = llvm.call @static_alloca(%cond1) : (i1) -> f32
  // CHECK: llvm.cond_br %{{.+}}, ^[[BB2:.+]], ^[[BB3:.+]]
  llvm.br ^bb3(%0: f32)
  // Make sure the lifetime end intrinsic has been inserted at both former
  // return sites of the callee.
  // CHECK: ^[[BB2]]:
  // CHECK-NEXT: llvm.load
  // CHECK-NEXT: llvm.intr.lifetime.end %[[PTR]]
  // CHECK: ^[[BB3]]:
  // CHECK-NEXT: llvm.intr.lifetime.end %[[PTR]]
^bb2:
  llvm.br ^bb3(%funcArg: f32)
^bb3(%blockArg: f32):
  llvm.return %blockArg : f32
}

// -----

llvm.func @static_alloca() -> f32 {
  %0 = llvm.mlir.constant(4 : i32) : i32
  %1 = llvm.alloca %0 x f32 : (i32) -> !llvm.ptr
  %2 = llvm.load %1 : !llvm.ptr -> f32
  llvm.return %2 : f32
}

// CHECK-LABEL: llvm.func @test_inline
llvm.func @test_inline(%cond0 : i1) {
  // Verify the alloca is relocated to the entry block of the parent function
  // if the region operation is neither marked as isolated from above or
  // automatic allocation scope.
  // CHECK: %[[ALLOCA:.+]] = llvm.alloca
  // CHECK: "test.one_region_op"() ({
  "test.one_region_op"() ({
    %0 = llvm.call @static_alloca() : () -> f32
    // CHECK-NEXT: llvm.intr.lifetime.start %[[ALLOCA]]
    // CHECK-NEXT: %[[RES:.+]] = llvm.load %[[ALLOCA]]
    // CHECK-NEXT: llvm.intr.lifetime.end %[[ALLOCA]]
    // CHECK-NEXT: test.region_yield %[[RES]]
    test.region_yield %0 : f32
  }) : () -> ()
  // Verify the alloca is not relocated out of operations that are marked as
  // isolated from above.
  // CHECK-NOT: llvm.alloca
  // CHECK: test.isolated_regions
  test.isolated_regions {
    // CHECK: %[[ALLOCA:.+]] = llvm.alloca
    %0 = llvm.call @static_alloca() : () -> f32
    // CHECK: test.region_yield
    test.region_yield %0 : f32
  }
  // Verify the alloca is not relocated out of operations that are marked as
  // automatic allocation scope.
  // CHECK-NOT: llvm.alloca
  // CHECK: test.alloca_scope_region
  test.alloca_scope_region {
    // CHECK: %[[ALLOCA:.+]] = llvm.alloca
    %0 = llvm.call @static_alloca() : () -> f32
    // CHECK: test.region_yield
    test.region_yield %0 : f32
  }
  llvm.return
}

// -----

llvm.func @alloca_with_lifetime(%cond: i1) -> f32 {
  %0 = llvm.mlir.constant(4 : i32) : i32
  %1 = llvm.alloca %0 x f32 : (i32) -> !llvm.ptr
  llvm.intr.lifetime.start %1 : !llvm.ptr
  %2 = llvm.load %1 : !llvm.ptr -> f32
  llvm.intr.lifetime.end %1 : !llvm.ptr
  %3 = llvm.fadd %2, %2 : f32
  llvm.return %3 : f32
}

// CHECK-LABEL: llvm.func @test_inline
llvm.func @test_inline(%cond0 : i1, %cond1 : i1, %funcArg : f32) -> f32 {
  // CHECK-NOT: llvm.cond_br
  // CHECK: %[[PTR:.+]] = llvm.alloca
  // CHECK: llvm.cond_br %{{.+}}, ^[[BB1:.+]], ^{{.+}}
  llvm.cond_br %cond0, ^bb1, ^bb2
  // CHECK: ^[[BB1]]
^bb1:
  // Make sure the original lifetime intrinsic has been preserved, rather than
  // inserting a new one with a larger scope.
  // CHECK: llvm.intr.lifetime.start %[[PTR]]
  // CHECK-NEXT: llvm.load %[[PTR]]
  // CHECK-NEXT: llvm.intr.lifetime.end %[[PTR]]
  // CHECK: llvm.fadd
  // CHECK-NOT: llvm.intr.lifetime.end
  %0 = llvm.call @alloca_with_lifetime(%cond1) : (i1) -> f32
  llvm.br ^bb3(%0: f32)
^bb2:
  llvm.br ^bb3(%funcArg: f32)
^bb3(%blockArg: f32):
  llvm.return %blockArg : f32
}

// -----

llvm.func @with_byval_arg(%ptr : !llvm.ptr { llvm.byval = f64 }) {
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval
// CHECK-SAME: %[[PTR:[a-zA-Z0-9_]+]]: !llvm.ptr
llvm.func @test_byval(%ptr : !llvm.ptr) {
  // Make sure the new static alloca goes to the entry block.
  // CHECK: %[[ALLOCA:.+]] = llvm.alloca %{{.+}} x f64
  // CHECK: llvm.br ^[[BB1:[a-zA-Z0-9_]+]]
  llvm.br ^bb1
  // CHECK: ^[[BB1]]
^bb1:
  // CHECK: "llvm.intr.memcpy"(%[[ALLOCA]], %[[PTR]]
  llvm.call @with_byval_arg(%ptr) : (!llvm.ptr) -> ()
  llvm.br ^bb2
^bb2:
  llvm.return
}

// -----

llvm.func @with_byval_arg(%ptr : !llvm.ptr { llvm.byval = f64 }) attributes {memory_effects = #llvm.memory_effects<other = readwrite, argMem = read, inaccessibleMem = readwrite, errnoMem  = none, targetMem0 = none, targetMem1 = none>} {
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval_read_only
// CHECK-NOT: llvm.call
// CHECK-NEXT: llvm.return
llvm.func @test_byval_read_only(%ptr : !llvm.ptr) {
  llvm.call @with_byval_arg(%ptr) : (!llvm.ptr) -> ()
  llvm.return
}

// -----

llvm.func @with_byval_arg(%ptr : !llvm.ptr { llvm.byval = f64 }) attributes {memory_effects = #llvm.memory_effects<other = readwrite, argMem = write, inaccessibleMem = readwrite, errnoMem  = none, targetMem0 = none, targetMem1 = none>} {
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval_write_only
// CHECK-SAME: %[[PTR:[a-zA-Z0-9_]+]]: !llvm.ptr
// CHECK: %[[ALLOCA:.+]] = llvm.alloca %{{.+}} x f64
// CHECK: "llvm.intr.memcpy"(%[[ALLOCA]], %[[PTR]]
llvm.func @test_byval_write_only(%ptr : !llvm.ptr) {
  llvm.call @with_byval_arg(%ptr) : (!llvm.ptr) -> ()
  llvm.return
}

// -----

llvm.func @aligned_byval_arg(%ptr : !llvm.ptr { llvm.byval = i16, llvm.align = 16 }) attributes {memory_effects = #llvm.memory_effects<other = read, argMem = read, inaccessibleMem = read, errnoMem  = none, targetMem0 = none, targetMem1 = none>} {
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval_input_aligned
// CHECK-SAME: %[[UNALIGNED:[a-zA-Z0-9_]+]]: !llvm.ptr
// CHECK-SAME: %[[ALIGNED:[a-zA-Z0-9_]+]]: !llvm.ptr
llvm.func @test_byval_input_aligned(%unaligned : !llvm.ptr, %aligned : !llvm.ptr { llvm.align = 16 }) {
  // Make sure only the unaligned input triggers a memcpy.
  // CHECK: %[[ALLOCA:.+]] = llvm.alloca %{{.+}} x i16 {alignment = 16
  // CHECK: "llvm.intr.memcpy"(%[[ALLOCA]], %[[UNALIGNED]]
  llvm.call @aligned_byval_arg(%unaligned) : (!llvm.ptr) -> ()
  // CHECK-NOT: memcpy
  llvm.call @aligned_byval_arg(%aligned) : (!llvm.ptr) -> ()
  llvm.return
}

// -----

llvm.func @func_that_uses_ptr(%ptr : !llvm.ptr)

llvm.func @aligned_byval_arg(%ptr : !llvm.ptr { llvm.byval = i16, llvm.align = 16 }) attributes {memory_effects = #llvm.memory_effects<other = read, argMem = read, inaccessibleMem = read, errnoMem  = none, targetMem0 = none, targetMem1 = none>} {
  llvm.call @func_that_uses_ptr(%ptr) : (!llvm.ptr) -> ()
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval_realign_alloca
llvm.func @test_byval_realign_alloca() {
  %size = llvm.mlir.constant(4 : i64) : i64
  // CHECK-NOT: llvm.alloca{{.+}}alignment = 1
  // CHECK: llvm.alloca {{.+}}alignment = 16 : i64
  // CHECK-NOT: llvm.intr.memcpy
  %unaligned = llvm.alloca %size x i16 { alignment = 1 } : (i64) -> !llvm.ptr
  llvm.call @aligned_byval_arg(%unaligned) : (!llvm.ptr) -> ()
  llvm.return
}

// -----

module attributes {
  dlti.dl_spec = #dlti.dl_spec<#dlti.dl_entry<"dlti.stack_alignment", 32 : i32>>
} {

llvm.func @func_that_uses_ptr(%ptr : !llvm.ptr)

llvm.func @aligned_byval_arg(%ptr : !llvm.ptr { llvm.byval = i16, llvm.align = 16 }) attributes {memory_effects = #llvm.memory_effects<other = read, argMem = read, inaccessibleMem = read, errnoMem  = none, targetMem0 = none, targetMem1 = none>} {
  llvm.call @func_that_uses_ptr(%ptr) : (!llvm.ptr) -> ()
  llvm.return
}

// CHECK-LABEL: llvm.func @test_exceeds_natural_stack_alignment
llvm.func @test_exceeds_natural_stack_alignment() {
  %size = llvm.mlir.constant(4 : i64) : i64
  // Natural stack alignment is exceeded, so prefer a copy instead of
  // triggering a dynamic stack realignment.
  // CHECK-DAG: %[[SRC:[a-zA-Z0-9_]+]] = llvm.alloca{{.+}}alignment = 2
  // CHECK-DAG: %[[DST:[a-zA-Z0-9_]+]] = llvm.alloca{{.+}}alignment = 16
  // CHECK: "llvm.intr.memcpy"(%[[DST]], %[[SRC]]
  %unaligned = llvm.alloca %size x i16 { alignment = 2 } : (i64) -> !llvm.ptr
  llvm.call @aligned_byval_arg(%unaligned) : (!llvm.ptr) -> ()
  llvm.return
}

}

// -----

module attributes {
  dlti.dl_spec = #dlti.dl_spec<#dlti.dl_entry<"dlti.stack_alignment", 32 : i32>>
} {

llvm.func @func_that_uses_ptr(%ptr : !llvm.ptr)

llvm.func @aligned_byval_arg(%ptr : !llvm.ptr { llvm.byval = i16, llvm.align = 16 }) attributes {memory_effects = #llvm.memory_effects<other = read, argMem = read, inaccessibleMem = read, errnoMem  = none, targetMem0 = none, targetMem1 = none>} {
  llvm.call @func_that_uses_ptr(%ptr) : (!llvm.ptr) -> ()
  llvm.return
}

// CHECK-LABEL: llvm.func @test_alignment_exceeded_anyway
llvm.func @test_alignment_exceeded_anyway() {
  %size = llvm.mlir.constant(4 : i64) : i64
  // Natural stack alignment is lower than the target alignment, but the
  // alloca's existing alignment already exceeds it, so we might as well avoid
  // the copy.
  // CHECK-NOT: llvm.alloca{{.+}}alignment = 1
  // CHECK: llvm.alloca {{.+}}alignment = 16 : i64
  // CHECK-NOT: llvm.intr.memcpy
  %unaligned = llvm.alloca %size x i16 { alignment = 8 } : (i64) -> !llvm.ptr
  llvm.call @aligned_byval_arg(%unaligned) : (!llvm.ptr) -> ()
  llvm.return
}

}

// -----

llvm.mlir.global private @unaligned_global(42 : i64) : i64
llvm.mlir.global private @aligned_global(42 : i64) { alignment = 64 } : i64

llvm.func @aligned_byval_arg(%ptr : !llvm.ptr { llvm.byval = i16, llvm.align = 16 }) attributes {memory_effects = #llvm.memory_effects<other = read, argMem = read, inaccessibleMem = read, errnoMem  = none, targetMem0 = none, targetMem1 = none>} {
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval_global
llvm.func @test_byval_global() {
  // Make sure only the unaligned global triggers a memcpy.
  // CHECK-DAG: %[[UNALIGNED:.+]] = llvm.mlir.addressof @unaligned_global
  // CHECK-DAG: %[[ALLOCA:.+]] = llvm.alloca
  // CHECK: "llvm.intr.memcpy"(%[[ALLOCA]], %[[UNALIGNED]]
  // CHECK-NOT: llvm.alloca
  %unaligned = llvm.mlir.addressof @unaligned_global : !llvm.ptr
  llvm.call @aligned_byval_arg(%unaligned) : (!llvm.ptr) -> ()
  %aligned = llvm.mlir.addressof @aligned_global : !llvm.ptr
  llvm.call @aligned_byval_arg(%aligned) : (!llvm.ptr) -> ()
  llvm.return
}

// -----

// Check that alignment information is preserved in the memcpy when inlining
// byval arguments.

llvm.func @byval_aligned_arg(%ptr : !llvm.ptr { llvm.byval = i32, llvm.align = 16 }) {
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval_memcpy_alignment
// CHECK-SAME: %[[PTR:[a-zA-Z0-9_]+]]: !llvm.ptr
llvm.func @test_byval_memcpy_alignment(%ptr : !llvm.ptr) {
  // Verify the memcpy carries the alignment info from the byval attribute.
  // CHECK: %[[ALLOCA:.+]] = llvm.alloca
  // CHECK: "llvm.intr.memcpy"(%[[ALLOCA]], %[[PTR]]
  // CHECK-SAME: {llvm.align = 16 : i64}
  llvm.call @byval_aligned_arg(%ptr) : (!llvm.ptr) -> ()
  llvm.return
}

// -----

// Check that inlining does not hoist byval allocas out of automatic allocation
// scopes, such as parallel forall regions. Each parallel iteration must have
// its own private copy of the byval argument.

llvm.func @byval_in_parallel(%ptr : !llvm.ptr { llvm.byval = f32 }) {
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval_in_parallel_region
// CHECK-SAME: %[[PTR:[a-zA-Z0-9_]+]]: !llvm.ptr
llvm.func @test_byval_in_parallel_region(%ptr : !llvm.ptr) {
  %c0 = arith.constant 0 : index
  // Verify the alloca is not hoisted out of the allocation scope.
  // CHECK-NOT: llvm.alloca
  // CHECK: test.alloca_scope_region
  test.alloca_scope_region {
    // CHECK: %[[ALLOCA:.+]] = llvm.alloca %{{.+}} x f32
    // CHECK: "llvm.intr.memcpy"(%[[ALLOCA]], %[[PTR]]
    llvm.call @byval_in_parallel(%ptr) : (!llvm.ptr) -> ()
    test.region_yield %c0 : index
  }
  llvm.return
}

// -----

// Check that inlining does not hoist byval allocas out of automatic allocation
// scopes, such as parallel forall regions. Each parallel iteration must have
// its own private copy of the byval argument.

llvm.func @byval_in_parallel(%ptr : !llvm.ptr { llvm.byval = f32 }) {
  llvm.return
}

// CHECK-LABEL: llvm.func @test_byval_in_parallel_region
// CHECK-SAME: %[[PTR:[a-zA-Z0-9_]+]]: !llvm.ptr
llvm.func @test_byval_in_parallel_region(%ptr : !llvm.ptr) {
  %c0 = arith.constant 0 : index
  // Verify the alloca is not hoisted out of the allocation scope.
  // CHECK-NOT: llvm.alloca
  // CHECK: test.alloca_scope_region
  test.alloca_scope_region {
    // CHECK: %[[ALLOCA:.+]] = llvm.alloca %{{.+}} x f32
    // CHECK: "llvm.intr.memcpy"(%[[ALLOCA]], %[[PTR]]
    llvm.call @byval_in_parallel(%ptr) : (!llvm.ptr) -> ()
    test.region_yield %c0 : index
  }
  llvm.return
}

// -----

llvm.func @ignored_attrs(%ptr : !llvm.ptr { llvm.inreg, llvm.nocapture, llvm.nofree, llvm.preallocated = i32, llvm.returned, llvm.alignstack = 32 : i64, llvm.writeonly, llvm.noundef, llvm.nonnull }, %x : i32 { llvm.zeroext }) -> (!llvm.ptr { llvm.noundef, llvm.inreg, llvm.nonnull }) {
  llvm.return %ptr : !llvm.ptr
}

// CHECK-LABEL: @test_ignored_attrs
// CHECK-NOT: llvm.call
// CHECK-NEXT: llvm.return
llvm.func @test_ignored_attrs(%ptr : !llvm.ptr, %x : i32) {
  llvm.call @ignored_attrs(%ptr, %x) : (!llvm.ptr, i32) -> (!llvm.ptr)
  llvm.return
}

// -----

llvm.func @disallowed_arg_attr(%ptr : !llvm.ptr { llvm.inalloca = i64 }) {
  llvm.return
}

// CHECK-LABEL: @test_disallow_arg_attr
// CHECK-NEXT: llvm.call
llvm.func @test_disallow_arg_attr(%ptr : !llvm.ptr) {
  llvm.call @disallowed_arg_attr(%ptr) : (!llvm.ptr) -> ()
  llvm.return
}

// -----

#callee = #llvm.access_group<id = distinct[0]<>>
#caller = #llvm.access_group<id = distinct[1]<>>

llvm.func @inlinee(%ptr : !llvm.ptr) -> i32 {
  %0 = llvm.load %ptr { access_groups = [#callee] } : !llvm.ptr -> i32
  llvm.return %0 : i32
}

// CHECK-DAG: #[[$CALLEE:.*]] = #llvm.access_group<id = {{.*}}>
// CHECK-DAG: #[[$CALLER:.*]] = #llvm.access_group<id = {{.*}}>

// CHECK-LABEL: func @caller
// CHECK: llvm.load
// CHECK-SAME: access_groups = [#[[$CALLEE]], #[[$CALLER]]]
llvm.func @caller(%ptr : !llvm.ptr) -> i32 {
  %0 = llvm.call @inlinee(%ptr) { access_groups = [#caller] } : (!llvm.ptr) -> (i32)
  llvm.return %0 : i32
}

// -----

#caller = #llvm.access_group<id = distinct[1]<>>

llvm.func @inlinee(%ptr : !llvm.ptr) -> i32 {
  %0 = llvm.load %ptr : !llvm.ptr -> i32
  llvm.return %0 : i32
}

// CHECK-DAG: #[[$CALLER:.*]] = #llvm.access_group<id = {{.*}}>

// CHECK-LABEL: func @caller
// CHECK: llvm.load
// CHECK-SAME: access_groups = [#[[$CALLER]]]
// CHECK: llvm.store
// CHECK-SAME: access_groups = [#[[$CALLER]]]
llvm.func @caller(%ptr : !llvm.ptr) -> i32 {
  %c5 = llvm.mlir.constant(5 : i32) : i32
  %0 = llvm.call @inlinee(%ptr) { access_groups = [#caller] } : (!llvm.ptr) -> (i32)
  llvm.store %c5, %ptr { access_groups = [#caller] } : i32, !llvm.ptr
  llvm.return %0 : i32
}

// -----

llvm.func @vararg_func(...) {
  llvm.return
}

llvm.func @vararg_intrinrics() {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %list = llvm.alloca %0 x !llvm.struct<"struct.va_list_opaque", (ptr)> : (i32) -> !llvm.ptr
  // The vararg intinriscs should normally be part of a variadic function.
  // However, this test uses a non-variadic function to ensure the presence of
  // the intrinsic alone suffices to prevent inlining.
  llvm.intr.vastart %list : !llvm.ptr
  llvm.return
}

// A variadic callee that never reads its variadic arguments is inlinable; only
// the callee containing `llvm.intr.vastart` remains a call.
// CHECK-LABEL: func @caller
llvm.func @caller() {
  // CHECK-NOT: llvm.call @vararg_func
  llvm.call @vararg_func() vararg(!llvm.func<void (...)>) : () -> ()
  // CHECK: llvm.call @vararg_intrinrics()
  llvm.call @vararg_intrinrics() : () -> ()
  llvm.return
}

// -----

llvm.func @private_func(%a : i32) -> i32 attributes {sym_visibility = "private"} {
  llvm.return %a : i32
}

// CHECK-LABEL: func @caller
llvm.func @caller(%x : i32) -> i32 {
  // CHECK-NOT: llvm.call @private_func
  %z = llvm.call @private_func(%x) : (i32) -> (i32)
  llvm.return %z : i32
}

// -----

llvm.func @unreachable_func(%a : i32) -> i32 {
  "llvm.intr.trap"() : () -> ()
  llvm.unreachable
}

// CHECK-LABEL: func @caller
llvm.func @caller(%x : i32) -> i32 {
  // CHECK-NOT: llvm.call @unreachable_func
  // CHECK: llvm.intr.trap
  // CHECK: llvm.unreachable
  %z = llvm.call @unreachable_func(%x) : (i32) -> (i32)
  llvm.return %z : i32
}

// -----
// Check that @func is not inlined because of llvm.blocktag

func.func @func(%arg0 : i32) -> i32  {
  llvm.blocktag <id = 1>
  llvm.return %arg0 : i32
}

// CHECK-LABEL: @llvm_ret
func.func @llvm_ret(%arg0 : i32) -> i32 {
  // CHECK-NOT: llvm.blocktag
  // CHECK: %[[R:.*]] = call
  %res = call @func(%arg0) : (i32) -> (i32)
  // CHECK: return %[[R]]
  return %res : i32
}

// -----
// Regression test for https://github.com/llvm/llvm-project/issues/118766
// A callee whose body block ends with an unregistered (non-terminator) op used
// to crash the inliner. The inliner should skip such callees instead.

llvm.func @callee_malformed_terminator() -> i64 attributes {llvm.emit_c_interface} {
  // Unregistered op acting as a fake terminator -- the function has no llvm.return.
  "test.foo"() : () -> ()
}

// CHECK-LABEL: @caller_malformed_terminator
llvm.func @caller_malformed_terminator() -> i64 attributes {llvm.emit_c_interface} {
  // CHECK: llvm.call @callee_malformed_terminator
  %0 = llvm.call @callee_malformed_terminator() : () -> i64
  llvm.return %0 : i64
}

// -----
// Complement to the above: a callee whose block ends with a registered
// non-LLVM terminator that genuinely has the IsTerminator trait (cf.br) does
// NOT trigger the guard. The call is inlined normally via the multi-block path
// (handleTerminator uses dyn_cast for non-llvm.return ops, so cf.br is left
// as-is and control flow reaches the llvm.return in the successor block).

llvm.func @callee_cf_br_terminator(%arg0 : i64) -> i64 {
  cf.br ^exit(%arg0 : i64)
^exit(%val : i64):
  llvm.return %val : i64
}

// CHECK-LABEL: @caller_cf_br_terminator
llvm.func @caller_cf_br_terminator(%arg0 : i64) -> i64 {
  // cf.br has IsTerminator, so isLegalToInline allows inlining.
  // CHECK-NOT: llvm.call @callee_cf_br_terminator
  // CHECK: llvm.return %arg0
  %0 = llvm.call @callee_cf_br_terminator(%arg0) : (i64) -> i64
  llvm.return %0 : i64
}

// -----

llvm.func @callee_noduplicate(%arg0: i32) -> i32 attributes { passthrough = ["noduplicate"] } {
  %0 = llvm.add %arg0, %arg0 : i32
  llvm.return %0 : i32
}

// `noduplicate` on the callee itself never blocks inlining in LLVM; only
// duplicating a noduplicate call out of a body does.
// CHECK-LABEL: llvm.func @caller
// CHECK-SAME: %[[ARG0:[a-zA-Z0-9_]+]]
// CHECK-NOT: llvm.call @callee_noduplicate
// CHECK: %[[RES:.+]] = llvm.add %[[ARG0]], %[[ARG0]]
// CHECK: llvm.return %[[RES]]
llvm.func @caller(%arg0: i32) -> i32 {
  %0 = llvm.call @callee_noduplicate(%arg0) : (i32) -> i32
  llvm.return %0 : i32
}

// -----

// The attribute has to sit on a declaration: a definition carrying it would
// simply be inlined into `@holder` first, leaving nothing to detect.
llvm.func @nodup() attributes { passthrough = ["noduplicate"] }

llvm.func @holder() {
  llvm.call @nodup() : () -> ()
  llvm.return
}

// Two callers means `@holder`'s region is cloned, which would duplicate its
// `@nodup` call. Both call sites are refused.
// CHECK-LABEL: llvm.func @caller1
// CHECK: llvm.call @holder
llvm.func @caller1() {
  llvm.call @holder() : () -> ()
  llvm.return
}

// CHECK-LABEL: llvm.func @caller2
// CHECK: llvm.call @holder
llvm.func @caller2() {
  llvm.call @holder() : () -> ()
  llvm.return
}

// -----

llvm.func @nodup() attributes { passthrough = ["noduplicate"] }

// `internal` linkage, not just private visibility: discardability follows the
// LLVM linkage, so an externally-linked callee would still be cloned.
llvm.func internal @sole_holder() attributes {sym_visibility = "private"} {
  llvm.call @nodup() : () -> ()
  llvm.return
}

// A single call to a discardable callee moves the region instead of copying
// it, so nothing is duplicated and inlining is allowed. This is LLVM's
// `isSoleCallToLocalFunction` exemption.
// CHECK-LABEL: llvm.func @caller
// CHECK-NOT: llvm.call @sole_holder
// CHECK: llvm.call @nodup
llvm.func @caller() {
  llvm.call @sole_holder() : () -> ()
  llvm.return
}

// -----

llvm.func @callee_returns_twice(%arg0: i32) -> i32 attributes { passthrough = ["returns_twice"] } {
  %0 = llvm.add %arg0, %arg0 : i32
  llvm.return %0 : i32
}

// `returns_twice` on the callee does not block inlining: LLVM treats it as a
// licence rather than a prohibition.
// CHECK-LABEL: llvm.func @caller
// CHECK-SAME: %[[ARG0:[a-zA-Z0-9_]+]]
// CHECK-NOT: llvm.call @callee_returns_twice
// CHECK: %[[RES:.+]] = llvm.add %[[ARG0]], %[[ARG0]]
// CHECK: llvm.return %[[RES]]
llvm.func @caller(%arg0: i32) -> i32 {
  %0 = llvm.call @callee_returns_twice(%arg0) : (i32) -> i32
  llvm.return %0 : i32
}

// -----

llvm.func @setjmp(!llvm.ptr) -> i32 attributes { passthrough = ["returns_twice"] }

llvm.func @callee_marked(%arg0: !llvm.ptr) -> i32 attributes { passthrough = ["returns_twice"] } {
  %0 = llvm.call @setjmp(%arg0) : (!llvm.ptr) -> i32
  llvm.return %0 : i32
}

// A callee that is itself marked `returns_twice` inlines even though its body
// calls a returns-twice function: the call site had already made the caller
// call one, so moving the body exposes nothing new.
// CHECK-LABEL: llvm.func @caller
// CHECK-NOT: llvm.call @callee_marked
// CHECK: llvm.call @setjmp
llvm.func @caller(%arg0: !llvm.ptr) -> i32 {
  %0 = llvm.call @callee_marked(%arg0) : (!llvm.ptr) -> i32
  llvm.return %0 : i32
}

// -----

llvm.func @setjmp(!llvm.ptr) -> i32 attributes { passthrough = ["returns_twice"] }

llvm.func @callee_unmarked(%arg0: !llvm.ptr) -> i32 {
  %0 = llvm.call @setjmp(%arg0) : (!llvm.ptr) -> i32
  llvm.return %0 : i32
}

// An unmarked callee whose body calls a returns-twice function is refused,
// which is the direction LLVM's `isInlineViable` actually guards.
// CHECK-LABEL: llvm.func @caller
// CHECK: llvm.call @callee_unmarked
// CHECK-NOT: llvm.call @setjmp
llvm.func @caller(%arg0: !llvm.ptr) -> i32 {
  %0 = llvm.call @callee_unmarked(%arg0) : (!llvm.ptr) -> i32
  llvm.return %0 : i32
}

// -----

llvm.func @sink() -> i32

llvm.func @musttail_wrapper() -> i32 {
  %0 = llvm.call musttail @sink() : () -> i32
  llvm.return %0 : i32
}

llvm.func @tail_wrapper() -> i32 {
  %0 = llvm.call tail @sink() : () -> i32
  llvm.return %0 : i32
}

llvm.func @notail_wrapper() -> i32 {
  %0 = llvm.call notail @sink() : () -> i32
  llvm.return %0 : i32
}

// Inlining through a non-tail call site weakens the inlined markers, as in
// LLVM: `f -> g -> musttail h  ==>  f -> h`. Leaving `musttail` in place would
// produce IR LLVM's verifier rejects, since the call no longer precedes a
// return of its own value. `notail` is preserved.
// CHECK-LABEL: llvm.func @caller
// CHECK-NOT: llvm.call musttail @sink
// CHECK-NOT: llvm.call tail @sink
// CHECK: llvm.call @sink
// CHECK: llvm.call @sink
// CHECK: llvm.call notail @sink
llvm.func @caller() -> i32 {
  %0 = llvm.call @musttail_wrapper() : () -> i32
  %1 = llvm.call @tail_wrapper() : () -> i32
  %2 = llvm.call @notail_wrapper() : () -> i32
  %3 = llvm.add %0, %1 : i32
  %4 = llvm.add %3, %2 : i32
  llvm.return %4 : i32
}

// -----

llvm.func @sink() -> i32

llvm.func @musttail_callee() -> i32 {
  %0 = llvm.call musttail @sink() : () -> i32
  llvm.return %0 : i32
}

// A musttail call site is the one case where the marker would have to survive
// inlining, and nothing keeps it adjacent to the caller's return afterwards, so
// the call is refused instead.
// CHECK-LABEL: llvm.func @caller
// CHECK: llvm.call musttail @musttail_callee
llvm.func @caller() -> i32 {
  %0 = llvm.call musttail @musttail_callee() : () -> i32
  llvm.return %0 : i32
}

// -----

llvm.func @sink() -> i32

llvm.func @musttail_wrapper() -> i32 {
  %0 = llvm.call musttail @sink() : () -> i32
  llvm.return %0 : i32
}

// `notail` on the call site counts as no marker, so the child `musttail` is
// weakened rather than carried into a position where it no longer precedes the
// return. Comparing the raw enumerators would keep it, since notail sorts above
// musttail.
// CHECK-LABEL: llvm.func @caller
// CHECK-NOT: llvm.call musttail @sink
// CHECK: llvm.call @sink
// CHECK: llvm.add
llvm.func @caller() -> i32 {
  %0 = llvm.call notail @musttail_wrapper() : () -> i32
  %1 = llvm.mlir.constant(1 : i32) : i32
  %2 = llvm.add %0, %1 : i32
  llvm.return %2 : i32
}

// -----

llvm.func @sink() -> i32

llvm.func @musttail_with_alloca() -> i32 {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %p = llvm.alloca %0 x i32 : (i32) -> !llvm.ptr
  llvm.store %0, %p : i32, !llvm.ptr
  %r = llvm.call musttail @sink() : () -> i32
  llvm.return %r : i32
}

// The callee's alloca makes the inliner insert `llvm.intr.lifetime.end` before
// the return, which would separate a preserved `musttail` from it. Refused for
// the same reason as above; this is the shape that made the refusal necessary
// rather than merely conservative.
// CHECK-LABEL: llvm.func @caller
// CHECK: llvm.call musttail @musttail_with_alloca
llvm.func @caller(%c: i1) -> i32 {
  llvm.cond_br %c, ^bb1, ^bb2
^bb1:
  %r = llvm.call musttail @musttail_with_alloca() : () -> i32
  llvm.return %r : i32
^bb2:
  %z = llvm.mlir.constant(0 : i32) : i32
  llvm.return %z : i32
}

// -----

llvm.func @indirect_br_callee(%addr: !llvm.ptr) -> i32 {
  llvm.indirectbr %addr : !llvm.ptr, [
  ^bb1,
  ^bb2
  ]
^bb1:
  %0 = llvm.mlir.constant(1 : i32) : i32
  llvm.return %0 : i32
^bb2:
  %1 = llvm.mlir.constant(2 : i32) : i32
  llvm.return %1 : i32
}

// An `llvm.indirectbr` branches to `llvm.blockaddress` values naming the
// callee's blocks. Cloning the body would leave the copy branching into the
// original function, so the callee is not inlinable. Note the addresses are
// taken outside the callee here, so the `llvm.blocktag` rejection does not
// cover this.
// CHECK-LABEL: llvm.func @caller
// CHECK: llvm.call @indirect_br_callee
llvm.func @caller(%addr: !llvm.ptr) -> i32 {
  %0 = llvm.call @indirect_br_callee(%addr) : (!llvm.ptr) -> i32
  llvm.return %0 : i32
}

// -----

llvm.func @personality() -> i32
llvm.func @sink()

llvm.func @callee() attributes { personality = @personality } {
  llvm.invoke @sink() to ^bb1 unwind ^bb2 : () -> ()
^bb1:
  llvm.return
^bb2:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i64)>
  llvm.resume %0 : !llvm.struct<(ptr, i64)>
}

// The caller's exception value type disagrees with the callee's. Inlining
// would trip `LLVMFuncOp::verify`'s landingpad/resume consistency rule, so the
// call has to stay.
// CHECK-LABEL: llvm.func @caller
// CHECK: llvm.call @callee
llvm.func @caller() attributes { personality = @personality } {
  llvm.invoke @sink() to ^bb1 unwind ^bb2 : () -> ()
^bb1:
  llvm.call @callee() : () -> ()
  llvm.return
^bb2:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  llvm.resume %0 : !llvm.struct<(ptr, i32)>
}

// -----

llvm.func @personality() -> i32
llvm.func @sink()

llvm.func @callee() attributes { personality = @personality } {
  llvm.invoke @sink() to ^bb1 unwind ^bb2 : () -> ()
^bb1:
  llvm.return
^bb2:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  llvm.resume %0 : !llvm.struct<(ptr, i32)>
}

// Agreeing exception value types inline, so the guard above is a type check
// and not a blanket refusal of callees with landing pads.
// CHECK-LABEL: llvm.func @caller
// CHECK-NOT: llvm.call @callee
llvm.func @caller() attributes { personality = @personality } {
  llvm.invoke @sink() to ^bb1 unwind ^bb2 : () -> ()
^bb1:
  llvm.call @callee() : () -> ()
  llvm.return
^bb2:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  llvm.resume %0 : !llvm.struct<(ptr, i32)>
}

// -----

llvm.func @personality() -> i32
llvm.func @sink()

// A callee that only rethrows carries an exception value type without any
// personality attribute, so the guard cannot be gated on the personality.
llvm.func @rethrow(%e: !llvm.struct<(ptr, i64)>) {
  llvm.resume %e : !llvm.struct<(ptr, i64)>
}

// CHECK-LABEL: llvm.func @caller
// CHECK: llvm.call @rethrow
llvm.func @caller(%e: !llvm.struct<(ptr, i64)>) attributes { personality = @personality } {
  llvm.invoke @sink() to ^bb1 unwind ^bb2 : () -> ()
^bb1:
  llvm.call @rethrow(%e) : (!llvm.struct<(ptr, i64)>) -> ()
  llvm.return
^bb2:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  llvm.resume %0 : !llvm.struct<(ptr, i32)>
}

// -----

llvm.func @personality() -> i32
llvm.func @sink()

llvm.func @callee_i32() attributes { personality = @personality } {
  llvm.invoke @sink() to ^bb1 unwind ^bb2 : () -> ()
^bb1:
  llvm.return
^bb2:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  llvm.resume %0 : !llvm.struct<(ptr, i32)>
}

llvm.func @callee_i64() attributes { personality = @personality } {
  llvm.invoke @sink() to ^bb1 unwind ^bb2 : () -> ()
^bb1:
  llvm.return
^bb2:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i64)>
  llvm.resume %0 : !llvm.struct<(ptr, i64)>
}

// A caller with no exception handling of its own acquires a type from the
// first callee inlined into it, which then constrains the second call site.
// The caller's type must therefore be recomputed per call site, never cached.
// CHECK-LABEL: llvm.func @caller
// CHECK-NOT: llvm.call @callee_i32
// CHECK: llvm.call @callee_i64
llvm.func @caller() {
  llvm.call @callee_i32() : () -> ()
  llvm.call @callee_i64() : () -> ()
  llvm.return
}

// -----

llvm.func @resume_only(%e: !llvm.struct<(ptr, i32)>) {
  llvm.resume %e : !llvm.struct<(ptr, i32)>
}

// A single-block callee whose terminator is not `llvm.return` must not take
// the single-block fast path, which can only rewrite an `llvm.return`. The
// body is still inlined, via the multi-block path.
// CHECK-LABEL: llvm.func @caller
// CHECK-SAME: %[[E:[a-zA-Z0-9_]+]]
// CHECK-NOT: llvm.call @resume_only
// CHECK: llvm.resume %[[E]]
llvm.func @caller(%e: !llvm.struct<(ptr, i32)>) {
  llvm.call @resume_only(%e) : (!llvm.struct<(ptr, i32)>) -> ()
  llvm.return
}

// -----

llvm.func @callee_strictfp() -> f32 attributes { passthrough = ["strictfp"] } {
  %0 = llvm.mlir.constant(1.000000e+00 : f32) : f32
  llvm.return %0 : f32
}

// A strictfp caller can absorb a strictfp callee: its FP operations are
// already constrained, so there is nothing to convert.
// CHECK-LABEL: llvm.func @strictfp_caller
// CHECK-NOT: llvm.call @callee_strictfp
// CHECK: %[[CST:.+]] = llvm.mlir.constant(1.000000e+00 : f32)
// CHECK: llvm.return %[[CST]]
llvm.func @strictfp_caller() -> f32 attributes { passthrough = ["strictfp"] } {
  %0 = llvm.call @callee_strictfp() : () -> f32
  llvm.return %0 : f32
}

// -----

llvm.func @vararg_fixed_args(%arg0: i32, ...) -> i32 {
  %0 = llvm.add %arg0, %arg0 : i32
  llvm.return %0 : i32
}

// A variadic callee is inlinable at a call site passing exactly its fixed
// arguments.
// CHECK-LABEL: llvm.func @caller
// CHECK-SAME: %[[ARG0:[a-zA-Z0-9_]+]]
// CHECK-NOT: llvm.call @vararg_fixed_args
// CHECK: %[[RES:.+]] = llvm.add %[[ARG0]], %[[ARG0]]
// CHECK: llvm.return %[[RES]]
llvm.func @caller(%arg0: i32) -> i32 {
  %0 = llvm.call @vararg_fixed_args(%arg0) vararg(!llvm.func<i32 (i32, ...)>) : (i32) -> i32
  llvm.return %0 : i32
}

// -----

llvm.func @vararg_generic_vastart(%arg0: i32, ...) -> i32 {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %list = llvm.alloca %0 x !llvm.struct<"va_list", (ptr)> : (i32) -> !llvm.ptr
  llvm.call_intrinsic "llvm.va_start.p0"(%list) : (!llvm.ptr) -> ()
  llvm.return %arg0 : i32
}

// `va_start` spelled as a generic intrinsic call blocks inlining just like the
// dedicated `llvm.intr.vastart` op does. Otherwise it would land in a caller
// that is not variadic, which LLVM rejects with "va_start called in a
// non-varargs function".
// CHECK-LABEL: llvm.func @caller
// CHECK: llvm.call @vararg_generic_vastart
llvm.func @caller(%arg0: i32) -> i32 {
  %0 = llvm.call @vararg_generic_vastart(%arg0) vararg(!llvm.func<i32 (i32, ...)>) : (i32) -> i32
  llvm.return %0 : i32
}

// -----

llvm.func @vararg_extra_args(%arg0: i32, ...) -> i32 {
  %0 = llvm.add %arg0, %arg0 : i32
  llvm.return %0 : i32
}

// A call passing more operands than the callee has fixed parameters is not
// inlined: there is no callee block argument to map the trailing operands to.
// CHECK-LABEL: llvm.func @caller
// CHECK: llvm.call @vararg_extra_args
llvm.func @caller(%arg0: i32) -> i32 {
  %0 = llvm.call @vararg_extra_args(%arg0, %arg0) vararg(!llvm.func<i32 (i32, ...)>) : (i32, i32) -> i32
  llvm.return %0 : i32
}
