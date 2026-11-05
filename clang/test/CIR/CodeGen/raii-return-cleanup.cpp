// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Regression test: RAII destructors at function return must execute and not
// end up in dead (unreachable) blocks.

static int result = 0;

struct S {
    int id;
    S(int id) : id(id) {}
    ~S() { result += id; }
};

int single_raii() {
    S a(10);
    return 0;
}

// CIR: cir.func {{.*}} @_Z11single_raiiv() -> {{.*}}!s32i
// CIR:   cir.call @_ZN1SC1Ei
// CIR:   %{{.*}} = cir.const #cir.int<0> : !s32i
// CIR:   cir.call @_ZN1SD1Ev
// CIR:   cir.return %{{.*}} : !s32i
// CIR-NOT: // no predecessors

// LLVM: define {{.*}} noundef i32 @_Z11single_raiiv()
// LLVM:   call void @_ZN1SC1Ei
// LLVM:   call void @_ZN1SD1Ev
// LLVM:   ret i32 0
// LLVM-NOT: ; No predecessors!

int multi_raii() {
    S a(1);
    S b(2);
    return 0;
}

// CIR: cir.func {{.*}} @_Z10multi_raiiv() -> {{.*}}!s32i
// CIR:   cir.call @_ZN1SC1Ei(%{{.*}}, %{{.*}})
// CIR:   cir.call @_ZN1SC1Ei(%{{.*}}, %{{.*}})
// CIR:   %{{.*}} = cir.const #cir.int<0> : !s32i
// CIR:   cir.call @_ZN1SD1Ev(%{{.*}})
// CIR:   cir.call @_ZN1SD1Ev(%{{.*}})
// CIR:   cir.return %{{.*}} : !s32i
// CIR-NOT: // no predecessors
