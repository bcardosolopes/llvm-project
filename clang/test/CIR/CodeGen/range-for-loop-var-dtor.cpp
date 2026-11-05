// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that range-for loop variables with non-trivial destructors are
// destroyed at the end of each iteration, not just at loop exit.
//
// Previously, the loop variable was emitted without a per-iteration scope,
// so its destructor only ran at loop exit. Each iteration's copy construction
// overwrote the previous value without destroying it first.

struct G {
    G(const G&);
    ~G();
};

void test_range_for_copy(const G* begin, const G* end) {
    struct Range {
        const G* b;
        const G* e;
        const G* begin() const { return b; }
        const G* end() const { return e; }
    };
    Range r{begin, end};
    for (G item : r) {
        // item should be destroyed at the end of each iteration
    }
}

// CHECK: define {{.*}}@_Z19test_range_for_copy
// The loop body should copy-construct item and then destroy it within the
// same iteration, before branching back to the loop condition.
// CHECK: call void @_ZN1GC1ERKS_(
// CHECK: call void @_ZN1GD1Ev(
// CHECK: br label %
