// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s
// The two attributes are new, and `LLVMDialectBytecode.td` is a second,
// hand-maintained copy of the attribute definitions that nothing warns about
// when it drifts. Check the bytecode round trip as well as the textual one.
// RUN: mlir-opt --verify-roundtrip -split-input-file %s

llvm.func @alloc(i64) -> !llvm.ptr

// CHECK-LABEL: @simple_memprof
llvm.func @simple_memprof() -> !llvm.ptr {
  %0 = llvm.mlir.constant(16 : i64) : i64
  // CHECK: call ptr @alloc({{.*}}), !memprof ![[MEMPROF:[0-9]+]], !callsite ![[STACK:[0-9]+]]
  %1 = llvm.call @alloc(%0) {
    llvm.callsite = array<i64: 123456789>,
    llvm.memprof = [#llvm.memprof_mib<call_stack = [123456789], tags = ["cold"]>]
  } : (i64) -> !llvm.ptr
  llvm.return %1 : !llvm.ptr
}

// CHECK-DAG: ![[MEMPROF]] = !{![[MIB:[0-9]+]]}
// CHECK-DAG: ![[MIB]] = !{![[STACK]], !"cold"}
// CHECK-DAG: ![[STACK]] = !{i64 123456789}

// -----

llvm.func @alloc(i64) -> !llvm.ptr

// The tags come before the (context id, total size) pairs: the verifier reads
// operands 1..N as strings until the first node, so the order is required.

// CHECK-LABEL: @rich_memprof
llvm.func @rich_memprof(%arg0: i64) -> !llvm.ptr {
  // CHECK: call ptr @alloc({{.*}}), !memprof ![[MEMPROF:[0-9]+]], !callsite ![[STACK2:[0-9]+]]
  %0 = llvm.call @alloc(%arg0) {
    llvm.callsite = array<i64: 333>,
    llvm.memprof = [
      #llvm.memprof_mib<call_stack = [111, 222], tags = ["cold"],
                        context_sizes = [#llvm.memprof_context_size<id = 999, size = 4096>]>,
      #llvm.memprof_mib<call_stack = [333], tags = ["notcold"]>]
  } : (i64) -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// CHECK-DAG: ![[MEMPROF]] = !{![[MIB1:[0-9]+]], ![[MIB2:[0-9]+]]}
// CHECK-DAG: ![[MIB1]] = !{![[STACK1:[0-9]+]], !"cold", ![[SIZES:[0-9]+]]}
// CHECK-DAG: ![[STACK1]] = !{i64 111, i64 222}
// CHECK-DAG: ![[SIZES]] = !{i64 999, i64 4096}
// CHECK-DAG: ![[MIB2]] = !{![[STACK2]], !"notcold"}
// CHECK-DAG: ![[STACK2]] = !{i64 333}
