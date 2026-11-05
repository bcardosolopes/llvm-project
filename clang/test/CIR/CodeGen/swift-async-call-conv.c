// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -target-cpu core2 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -target-cpu core2 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test swiftasync parameter attribute and implicit musttail for
// swiftasynccall functions calling other swiftasynccall functions.

#define SWIFTASYNCCALL __attribute__((swiftasynccall))
#define ASYNC_CONTEXT __attribute__((swift_async_context))

// CIR-LABEL: cir.func {{.*}}@async_leaf(
// CIR-SAME: {cir.noundef, cir.swiftasync}
// CIR-SAME: cc(swift_async)

// LLVM-LABEL: define swifttailcc void @async_leaf(
// LLVM-SAME: swiftasync
SWIFTASYNCCALL void async_leaf(char * ASYNC_CONTEXT ctx) {
  *ctx += 1;
}

// CIR-LABEL: cir.func {{.*}}@async_caller(
// CIR-SAME: {cir.noundef, cir.swiftasync}
// CIR-SAME: cc(swift_async)

// The return call should be musttail (implicit for swiftasynccall).
// LLVM-LABEL: define swifttailcc void @async_caller(
// LLVM-SAME: swiftasync
// LLVM: musttail call swifttailcc void @async_leaf(
// LLVM-NEXT: ret void
SWIFTASYNCCALL void async_caller(_Bool b, char * ASYNC_CONTEXT ctx) {
  if (b) {
    return async_leaf(ctx);
  }
  return async_leaf(ctx);
}

// Non-return calls to swiftasynccall should NOT be musttail.
// LLVM-LABEL: define swifttailcc void @async_non_tail(
// LLVM: call swifttailcc void @async_leaf(
// LLVM: musttail call swifttailcc void @async_leaf(
// LLVM-NEXT: ret void
SWIFTASYNCCALL void async_non_tail(char * ASYNC_CONTEXT ctx) {
  async_leaf(ctx);
  return async_leaf(ctx);
}

// Non-swiftasynccall callers should not musttail.
// CIR-LABEL: cir.func {{.*}}@normal_caller(
// CIR-NOT: cir.swiftasync
// CIR-SAME: extra(

// LLVM-LABEL: define {{.*}}void @normal_caller(
// LLVM-NOT: swiftasync
// LLVM-NOT: musttail
// LLVM: call swifttailcc void @async_leaf(
void normal_caller(char *p) {
  async_leaf(p);
}
