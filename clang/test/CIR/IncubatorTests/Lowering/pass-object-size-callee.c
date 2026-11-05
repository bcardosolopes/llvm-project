// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that the callee uses the hidden pass_object_size parameter
// directly instead of emitting @llvm.objectsize calls.

#define PS(N) __attribute__((pass_object_size(N)))

// LLVM-LABEL: define {{.*}} @ObjectSize0
// LLVM-NOT: @llvm.objectsize
// LLVM: ret i32
// OGCG-LABEL: define {{.*}} @ObjectSize0
// OGCG-NOT: @llvm.objectsize
// OGCG: ret i32
int ObjectSize0(void *const p PS(0)) {
    return __builtin_object_size(p, 0);
}

// Type 0 and type 1 are compatible (OGCG areBOSTypesCompatible).
// LLVM-LABEL: define {{.*}} @ObjectSize0to1
// LLVM-NOT: @llvm.objectsize
// LLVM: ret i32
// OGCG-LABEL: define {{.*}} @ObjectSize0to1
// OGCG-NOT: @llvm.objectsize
// OGCG: ret i32
int ObjectSize0to1(void *const p PS(0)) {
    return __builtin_object_size(p, 1);
}

// Type 2: min size.
// LLVM-LABEL: define {{.*}} @ObjectSize2
// LLVM-NOT: @llvm.objectsize
// LLVM: ret i32
// OGCG-LABEL: define {{.*}} @ObjectSize2
// OGCG-NOT: @llvm.objectsize
// OGCG: ret i32
int ObjectSize2(void *const p PS(2)) {
    return __builtin_object_size(p, 2);
}

// Type 3 and type 2 are compatible.
// LLVM-LABEL: define {{.*}} @ObjectSize3to2
// LLVM-NOT: @llvm.objectsize
// LLVM: ret i32
// OGCG-LABEL: define {{.*}} @ObjectSize3to2
// OGCG-NOT: @llvm.objectsize
// OGCG: ret i32
int ObjectSize3to2(void *const p PS(3)) {
    return __builtin_object_size(p, 2);
}
