// RUN: %clang_cc1 -std=c++98 -triple x86_64-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++98 -triple x86_64-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Tests that CXXBindTemporaryExpr is correctly handled in lvalue context,
// creating a temporary with proper destructor cleanup. Previously this
// hit an assertion: "binding l-value to type which needs a temporary".

struct A { A(int); ~A(); void *p; };
struct B { B(); ~B(); int x; };

// CIR-LABEL: @_Z1cv
// CIR: %{{.*}} = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["temp.lvalue"]
// The address of B::x is now computed before B's constructor call rather than
// after it. cir.get_member is pure address arithmetic on storage that already
// exists, so the order is immaterial to the result -- and CIRGen deliberately
// hoists pure ops ahead of a synthetic try to keep dominance (see
// synth-try-dominance-make-optional.cpp). get_member only became eligible for
// that once it stopped declaring a MemRead it never performed. The load of
// the member still happens after the constructor, which is the part that
// matters.
// CIR: cir.get_member %{{.*}}[0] {name = "x"}
// CIR: cir.call exception @_ZN1BC1Ev
// CIR: cir.load {{.*}}!cir.ptr<!s32i>, !s32i
// CIR: cir.call exception @_ZN1AC1Ei
// CIR: cir.call @_ZN1BD1Ev(%{{.*}}) {cir.terminate_on_throw}

// LLVM-LABEL: define {{.*}} @_Z1cv
// LLVM: invoke void @_ZN1BC1Ev
// LLVM: invoke void @_ZN1AC1Ei
// LLVM: invoke void @_ZN1BD1Ev
A *c() { return new A(B().x); }
