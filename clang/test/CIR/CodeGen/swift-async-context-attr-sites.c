// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// The swiftasync parameter attribute (from swift_async_context in a
// swiftasynccall function) must be emitted on the definition parameter, on
// call-site arguments, AND on the declaration -- matching OGCG. Dropping it on
// calls/declarations is a silent ABI miscompile: the callee reads its context
// from the Swift async-context register (r14 on x86_64), so a call site that
// omits swiftasync leaves the context in the ordinary argument register.

#define SWIFTASYNCCALL __attribute__((swiftasynccall))
#define ASYNC_CONTEXT __attribute__((swift_async_context))

SWIFTASYNCCALL void async_callee(void *ASYNC_CONTEXT ctx, int x);

// Definition parameter carries swiftasync, and the call-site argument does too.
// CHECK-LABEL: define dso_local swifttailcc void @async_fn(
// CHECK-SAME: ptr noundef swiftasync %{{[0-9]+}}, i32 noundef
// CHECK: call swifttailcc void @async_callee(ptr noundef swiftasync %{{[0-9]+}}, i32
SWIFTASYNCCALL void async_fn(void *ASYNC_CONTEXT ctx, int x) {
  async_callee(ctx, x + 1);
}

// The declaration carries swiftasync as well.
// CHECK: declare swifttailcc void @async_callee(ptr noundef swiftasync, i32 noundef)

// A call site in a plain (non-swiftasynccall) caller also carries swiftasync.
// CHECK-LABEL: define dso_local void @driver(
// CHECK: call swifttailcc void @async_fn(ptr noundef swiftasync %{{[0-9]+}}, i32
void driver(void *ctx) { async_fn(ctx, 41); }
