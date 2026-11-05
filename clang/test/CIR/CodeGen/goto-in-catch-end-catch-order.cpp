// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fexceptions -fcxx-exceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fexceptions -fcxx-exceptions %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// A goto that jumps within a catch handler (here, a forward goto over a later
// declaration) stays inside the handler. FlattenCFG must NOT treat it as a
// handler exit and emit __cxa_end_catch before it: the caught exception object
// must stay alive until control actually leaves the handler at its closing
// brace. Emitting end_catch on the intra-handler goto destroyed the exception
// object too early, mis-ordering it before the catch-body destructors (a silent
// wrong-dtor-order miscompile). Sibling of the break/continue-in-catch handling
// in catch-scope-break-continue.cpp.

struct T { int id; T(int i); ~T(); };
struct X { int id; X(int i); ~X(); };

int goto_in_catch() {
  try { throw X(61); }
  catch (X& e) {
    T a(63);
    if (1) goto L;
    L:;
    T c(69);
  }
  return 0;
}

// The normal catch path must be: begin_catch, construct a(63), (goto), then
// construct c(69), destroy c, destroy a, and only THEN __cxa_end_catch — with
// no premature __cxa_end_catch between the two constructions (the goto point).

// LLVM-LABEL: define {{.*}}@_Z13goto_in_catchv
// LLVM: call ptr @__cxa_begin_catch
// LLVM: invoke void @_ZN1TC1Ei(ptr {{.*}}i32 noundef 63)
// LLVM-NOT: @__cxa_end_catch
// LLVM: invoke void @_ZN1TC1Ei(ptr {{.*}}i32 noundef 69)
// LLVM: call void @_ZN1TD1Ev
// LLVM: call void @_ZN1TD1Ev
// LLVM: call void @__cxa_end_catch()

// OGCG-LABEL: define {{.*}}@_Z13goto_in_catchv
// OGCG: call ptr @__cxa_begin_catch
// OGCG: invoke void @_ZN1TC1Ei(ptr {{.*}}i32 noundef 63)
// OGCG-NOT: @__cxa_end_catch
// OGCG: invoke void @_ZN1TC1Ei(ptr {{.*}}i32 noundef 69)
// OGCG: call void @_ZN1TD1Ev
// OGCG: call void @_ZN1TD1Ev
// OGCG: call void @__cxa_end_catch()
