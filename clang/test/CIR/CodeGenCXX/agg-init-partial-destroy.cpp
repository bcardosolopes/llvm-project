// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -fexceptions -fcxx-exceptions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.og.ll %s

// Regression test: When aggregate-initializing a struct with
// multiple bases, partial-construction cleanups must be conditional. If the
// second base's constructor throws, only the first base should be destroyed
// (guarded by an active flag), not unconditionally.

struct Q { Q(); };
struct R { R(Q); ~R(); };
struct S { S(Q); ~S(); };
struct T : R, S {};
Q q;
T t { R{q}, S{q} };

// CIR-LABEL: @__cxx_global_var_init.1

// Two cleanup.cond flag allocas (one per base cleanup).
// CIR: cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
// CIR: cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]

// Construct R, then set its flag to true.
// CIR: cir.call @_ZN1RC1E1Q

// S constructor is an invoke (may throw). The enclosing cir.cleanup.scope's
// shared cleanup region guards the R base destruction behind a cir.if that
// loads the active flag, then destroys the R temporary unconditionally --
// the partial-construction cleanup is deactivated on the normal path but must
// still be tested on the unwind edge.
// CIR: cir.cleanup.scope {
// CIR:   cir.store align(1) %{{.*}}, %{{.*}} : !cir.bool, !cir.ptr<!cir.bool>
// CIR:   cir.call exception @_ZN1SC1E1Q
// On the normal path R's flag goes back to false and the temporaries die.
// CIR:   cir.store align(1) %{{.*}}, %{{.*}} : !cir.bool, !cir.ptr<!cir.bool>
// CIR:   cir.call @_ZN1SD1Ev
// CIR:   cir.yield
// CIR: } cleanup {
// CIR:   %[[FLAG:.*]] = cir.load align(1) %{{.*}} : !cir.ptr<!cir.bool>, !cir.bool
// CIR:   cir.if %[[FLAG]] {
// CIR:     cir.call @_ZN1RD1Ev
// CIR:   }
// CIR:   cir.call @_ZN1RD1Ev
// CIR:   cir.yield
// CIR: }
// CIR: cir.call @_ZN1RD1Ev

// LLVM-LABEL: define internal void @__cxx_global_var_init.1
// LLVM: call void @_ZN1RC1E1Q
// LLVM: store i8 1, ptr %[[FLAG:.*]], align 1
// LLVM: invoke void @_ZN1SC1E1Q
// Landing pad: load flag, branch conditionally.
// LLVM: %[[FLAGVAL:.*]] = load i8, ptr %[[FLAG]], align 1
// LLVM: %[[COND:.*]] = trunc i8 %[[FLAGVAL]] to i1
// LLVM: br i1 %[[COND]], label %[[ACTIVE:.*]], label %[[SKIP:.*]]
// LLVM: [[ACTIVE]]:
// LLVM: call void @_ZN1RD1Ev(ptr {{.*}}@t)
// LLVM: br label %[[SKIP]]
// LLVM: [[SKIP]]:
// LLVM: call void @_ZN1RD1Ev(ptr {{.*}}%{{.*}})

// OGCG-LABEL: define internal void @__cxx_global_var_init.1
// OGCG: call void @_ZN1RC1E1Q
// OGCG: store i1 true, ptr %[[FLAG:.*]], align 1
// OGCG: invoke void @_ZN1SC1E1Q
// Landing pad: load flag, branch conditionally.
// OGCG: %[[FLAGVAL:.*]] = load i1, ptr %[[FLAG]], align 1
// OGCG: br i1 %[[FLAGVAL]], label %[[ACTIVE:.*]], label %[[SKIP:.*]]
// OGCG: [[ACTIVE]]:
// OGCG: call void @_ZN1RD1Ev(ptr {{.*}}@t)
// OGCG: br label %[[SKIP]]
// OGCG: [[SKIP]]:
// OGCG: call void @_ZN1RD1Ev(ptr {{.*}}%{{.*}})
