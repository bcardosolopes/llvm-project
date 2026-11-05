// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -fno-clangir-call-conv-lowering -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that __attribute__((error)) guard fallback calls are eliminated when
// the user properly exits with break/return/continue inside a compound stmt.
// This matches OGCG's behavior where code after a terminator is not emitted.
// Reproducer (MCF GUARD_LET_WHERE_OR_ELSE macro pattern).

__attribute__((error("You need to exit")))
void guard_trap(void);

// CHECK-LABEL: @_Z10test_breaki
// CHECK-NOT: call void @_Z10guard_trapv
// CHECK: }
int test_break(int type) {
    int result = 0;
    switch (type) {
        case 1: {
            int *p = (int*)(long)type;
            if (p == 0) {
                // MCF macro pattern: { break; } followed by guard call
                { break; }
                guard_trap();
            }
            result = *p;
            break;
        }
        case 2:
            result = 42;
            break;
    }
    return result;
}

// CHECK-LABEL: @_Z11test_returnPv
// CHECK-NOT: call void @_Z10guard_trapv
// CHECK: }
void* test_return(void* p) {
    if (p == 0) {
        // MCF macro pattern: { { return nullptr; } } followed by guard call
        { { return nullptr; } }
        guard_trap();
    }
    return p;
}

// CHECK-LABEL: @_Z13test_continuev
// CHECK-NOT: call void @_Z10guard_trapv
// CHECK: }
void test_continue() {
    for (int i = 0; i < 10; i++) {
        if (i == 5) {
            { continue; }
            guard_trap();
        }
    }
}

// Negative test: guard call IS reachable when user doesn't exit.
// CHECK-LABEL: @_Z19test_reachable_trapPi
// CHECK: call void @_Z10guard_trapv
// CHECK: }
void test_reachable_trap(int *p) {
    if (p == 0) {
        // No exit — guard call should remain
        guard_trap();
    }
}
