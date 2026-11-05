// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

#define __maybe_undef __attribute__((maybe_undef))

// Callee with maybe_undef on second parameter.
void t1(int param1, int __maybe_undef param2, int param3) {}

// Caller: freeze should be emitted only on the argument corresponding to
// the maybe_undef parameter (param2).
void t2(int param1, int param2, int param3) {
    t1(param1, param2, param3);
}

// CIR-LABEL: cir.func {{.*}} @t2
// CIR:       %[[P1:.*]] = cir.load {{.*}} : !cir.ptr<!s32i>, !s32i
// CIR:       %[[P2:.*]] = cir.load {{.*}} : !cir.ptr<!s32i>, !s32i
// CIR:       %[[P3:.*]] = cir.load {{.*}} : !cir.ptr<!s32i>, !s32i
// CIR:       %[[FROZEN:.*]] = cir.freeze %[[P2]] : !s32i
// CIR:       cir.call @t1(%[[P1]], %[[FROZEN]], %[[P3]])

// LLVM-LABEL: @t2
// LLVM:       %[[V1:.*]] = load i32, ptr
// LLVM:       %[[V2:.*]] = load i32, ptr
// LLVM:       %[[V3:.*]] = load i32, ptr
// LLVM:       %[[F:.*]] = freeze i32 %[[V2]]
// LLVM:       call void @t1(i32 noundef %[[V1]], i32 noundef %[[F]], i32 noundef %[[V3]])

// OGCG-LABEL: @t2
// OGCG:       %[[V1:.*]] = load i32, ptr
// OGCG:       %[[V2:.*]] = load i32, ptr
// OGCG:       %[[V3:.*]] = load i32, ptr
// OGCG:       %[[F:.*]] = freeze i32 %[[V2]]
// OGCG:       call void @t1(i32 noundef %[[V1]], i32 noundef %[[F]], i32 noundef %[[V3]])

// Test: declaration with maybe_undef, definition without — freeze still applies.
void func(int param);
void func1(int __maybe_undef param);

void other() {
  int Var;
  func(Var);
  func1(Var);
}

void func(__maybe_undef int param) {}
void func1(int param) {}

// CIR-LABEL: cir.func {{.*}} @other
// CIR:       cir.call @func(
// CIR:       %[[VAL:.*]] = cir.load
// CIR:       %[[FROZEN2:.*]] = cir.freeze %[[VAL]] : !s32i
// CIR:       cir.call @func1(%[[FROZEN2]])

// LLVM-LABEL: @other
// LLVM:       call void @func(i32 noundef
// LLVM:       %[[L:.*]] = load i32, ptr
// LLVM:       %[[F2:.*]] = freeze i32 %[[L]]
// LLVM:       call void @func1(i32 noundef %[[F2]])

// OGCG-LABEL: @other
// OGCG:       call void @func(i32 noundef
// OGCG:       %[[L:.*]] = load i32, ptr
// OGCG:       %[[F2:.*]] = freeze i32 %[[L]]
// OGCG:       call void @func1(i32 noundef %[[F2]])

// Test: variadic function — freeze only the required maybe_undef param,
// not the variadic args.
void VariadicFunction(int __maybe_undef x, ...);
void TestVariadicFunction(int x) {
  int Var;
  VariadicFunction(x, Var, Var);
}

// CIR-LABEL: cir.func {{.*}} @TestVariadicFunction
// CIR:       %[[ARG:.*]] = cir.load {{.*}} : !cir.ptr<!s32i>, !s32i
// CIR:       %[[FROZEN3:.*]] = cir.freeze %[[ARG]] : !s32i
// CIR:       cir.call @VariadicFunction(%[[FROZEN3]],

// LLVM-LABEL: @TestVariadicFunction
// LLVM:       %[[A:.*]] = load i32, ptr
// LLVM:       %[[F3:.*]] = freeze i32 %[[A]]
// LLVM:       call void (i32, ...) @VariadicFunction(i32 noundef %[[F3]],

// OGCG-LABEL: @TestVariadicFunction
// OGCG:       %[[A:.*]] = load i32, ptr
// OGCG:       freeze i32
// OGCG:       call void (i32, ...) @VariadicFunction(i32 noundef
