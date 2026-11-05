// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat -fcxx-exceptions -fexceptions %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s --check-prefix=CIR-FLAT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// When an inner catch-all is inside an enclosing EH scope,
// __cxa_end_catch on the inner normal exit path must be emitted as invoke to
// the outer landing pad (not plain call).

void may_throw();
void g();

int f() {
  try {
    try {
      may_throw();
    } catch (...) {
      g();
    }
  } catch (...) {
    return 1;
  }
  return 0;
}

// LLVM-LABEL: define {{.*}} @_Z1fv
// LLVM: invoke void @_Z1gv()
// LLVM: invoke void @__cxa_end_catch()

// OGCG-LABEL: define {{.*}} @_Z1fv
// OGCG: invoke void @_Z1gv()
// OGCG: invoke void @__cxa_end_catch()

// CIR-FLAT-LABEL: cir.func{{.*}} @_Z1fv
// The inner catch body must end through a throw-capable __cxa_end_catch so the
// enclosing try can catch unwinds from that runtime call. Throws from inside
// the catch body still use the cleanup-only end_catch path.
// CIR-FLAT: cir.try_call @_Z1gv()
// CIR-FLAT: cir.catch_param end {cir.end_catch_eh_cleanup}
// CIR-FLAT: cir.try_call @__cxa_end_catch()
