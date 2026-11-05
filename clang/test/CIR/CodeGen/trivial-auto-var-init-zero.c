// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -ftrivial-auto-var-init=zero %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -ftrivial-auto-var-init=zero %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM --input-file=%t.ll

// Test that -ftrivial-auto-var-init=zero correctly initializes
// uninitialized local variables with zero values.

// Scalar int: zero-initialized.
int test_int(void) {
    int x;
    return x;
}

// CHECK: cir.func {{.*}} @test_int
// CHECK:   cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init]
// CHECK:   %[[ZERO:.*]] = cir.const #cir.int<0> : !s32i
// CHECK:   cir.store align(4) %[[ZERO]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// LLVM-LABEL: @test_int
// LLVM: ret i32 0

// Struct: zero-initialized.
struct S { int a; int b; };

void test_struct(void) {
    struct S s;
    (void)s;
}

// CHECK: cir.func {{.*}} @test_struct
// CHECK:   cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s", init]
// CHECK:   cir.libc.memset
// LLVM-LABEL: @test_struct
// LLVM: call void @llvm.memset{{.*}}, !annotation [[AUTO_INIT:![0-9]+]]

// Explicitly initialized variable: should NOT get double-initialized.
int test_explicit_init(void) {
    int x = 42;
    return x;
}

// CHECK: cir.func {{.*}} @test_explicit_init
// CHECK:   cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init]
// CHECK:   cir.const #cir.int<42>

// constexpr variable: should NOT get trivial init.
int test_constexpr(void) {
    _Static_assert(1, "ok");
    int x = 1;
    return x;
}

// Array: zero-initialized.
void test_array(void) {
    int arr[10];
    (void)arr;
}

// CHECK: cir.func {{.*}} @test_array
// CHECK:   cir.alloca !cir.array<!s32i x 10>
// CHECK:   cir.libc.memset
// LLVM-LABEL: @test_array
// LLVM: call void @llvm.memset{{.*}}, !annotation [[AUTO_INIT]]

// LLVM: [[AUTO_INIT]] = !{!"auto-init"}
