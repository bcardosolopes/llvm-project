// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fexceptions -fcxx-exceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Return from catch handler must call __cxa_end_catch before returning
// to destroy the exception object. Without this, the exception object leaks.

static int alive = 0;
struct Trackable {
    int val;
    Trackable(int v) : val(v) { alive++; }
    Trackable(const Trackable& o) : val(o.val) { alive++; }
    ~Trackable() { alive--; }
};

int capture_and_return() {
    try {
        throw Trackable(42);
    } catch (const Trackable& t) {
        return t.val;
    }
}

// Verify __cxa_end_catch is called before the return from catch handler.
// CHECK-LABEL: define {{.*}}@_Z18capture_and_returnv
// CHECK: call ptr @__cxa_begin_catch
// CHECK: call void @__cxa_end_catch()
// CHECK: ret i32

// Return nested inside if/scope within catch handler must also call
// __cxa_end_catch. The walk must find all cir.return ops, not just direct
// children of the catch handler region.
int nested_return_in_catch() {
    try {
        throw Trackable(42);
    } catch (Trackable& t) {
        if (t.val > 0)
            return t.val;
        return 0;
    }
}

// Both return paths (if-true and fallthrough) must call __cxa_end_catch.
// After cir-flatten-cfg's unifyRetvalMultiReturns pass, the two return paths
// store to the shared __retval slot, call __cxa_end_catch, then branch to a
// single unified return block — so we expect two end_catch calls but only one
// final ret. Each end_catch is paired with a store-to-retval and a branch.
// CHECK-LABEL: define {{.*}}@_Z22nested_return_in_catchv
// CHECK: call ptr @__cxa_begin_catch
// CHECK: store i32 {{.*}}, ptr %[[RETVAL:.*]],
// CHECK: call void @__cxa_end_catch()
// CHECK: store i32 0, ptr %[[RETVAL]],
// CHECK: call void @__cxa_end_catch()
// CHECK: load i32, ptr %[[RETVAL]],
// CHECK: ret i32
