// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that ConstantExpr lvalue bases in global initializers are handled
// correctly. This exercises ConstantLValueEmitter::VisitConstantExpr.

static constexpr int val = 42;

consteval const int& getRef() { return val; }

// Global pointer initialized with address of consteval reference result.
// The ConstantExpr wrapping the consteval call becomes the lvalue base.
const int* ptr = &getRef();

// CIR: @_ZL3val = #cir.int<42> : !s32i
// CIR: @ptr = #cir.global_view<@_ZL3val> : !cir.ptr<!s32i>

// OGCG: @_ZL3val = internal constant i32 42
// OGCG: @ptr = {{.*}}global ptr @_ZL3val

struct S {
  int a;
  int b;
};

static constexpr S s = {10, 20};

// consteval returning reference to struct member (with byte offset)
consteval const int& getB() { return s.b; }
const int* pb = &getB();

// CIR: @pb = #cir.global_view<@_ZL1s, [1 : i32]> : !cir.ptr<!s32i>
// OGCG: @pb = {{.*}}global ptr getelementptr (i8, ptr @_ZL1s, i64 4)

// consteval returning reference to array element (with byte offset)
static constexpr int arr[] = {1, 2, 3};

consteval const int& getElem() { return arr[1]; }
const int* pe = &getElem();

// CIR: @pe = #cir.global_view<@_ZL3arr, [1 : i32]> : !cir.ptr<!s32i>
// OGCG: @pe = {{.*}}global ptr getelementptr (i8, ptr @_ZL3arr, i64 4)
