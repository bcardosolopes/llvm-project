// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -std=c++17
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// A cleanup guarded by an activation flag sets the flag where the object is
// built, and the flag's alloca is in the entry block. The destructor has to
// clear it again, or a construction that runs more than once carries one
// evaluation's true into the next: the flag says an object is there while the
// storage holds the one already destroyed. It matters because a shared
// cir.cleanup.scope reads the flag on every unwind edge in its range,
// including edges taken before the object was built again, and destroys that
// stale object a second time.

struct A {
  A();
  ~A();
};
int use(const A &);
int pick();

void f() {
  for (int i = 0; i < 3; ++i)
    (void)(pick() ? use(A()) : 0);
}

// CIR-LABEL: cir.func {{.*}}@_Z1fv()
// CIR:   %[[TMP:.*]] = cir.alloca !rec_A, {{.*}} ["ref.tmp0"]
// CIR:   %[[FLAG:.*]] = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]

// Built: the flag goes true.
// CIR:   cir.call @_ZN1AC1Ev(%[[TMP]])
// CIR:   %[[TRUE:.*]] = cir.const #true
// CIR:   cir.store{{.*}} %[[TRUE]], %[[FLAG]] :

// Destroyed at the end of the full expression, guarded by that flag -- and
// the flag goes back to false, which is what the next iteration reads.
// CIR:   cir.if
// CIR-NEXT: cir.call @_ZN1AD1Ev(%[[TMP]])
// CIR-NEXT: %[[FALSE:.*]] = cir.const #false
// CIR-NEXT: cir.store{{.*}} %[[FALSE]], %[[FLAG]] :
