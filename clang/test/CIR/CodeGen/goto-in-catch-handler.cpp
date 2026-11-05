// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A forward `goto` whose label lives inside a catch handler used to abort
// FlattenCFG: the pass erased the (still predecessor-less) label block as dead
// while the `cir.goto` remained symbolic, so GotoSolver could not find the
// label. The handler cleanups (catch-variable dtor + __cxa_end_catch) also had
// to stay off the goto path, since the goto does not exit the handler.

struct S { ~S(); };
void may_throw();
void use();

void goto_in_catch_handler() {
  try {
    may_throw();
  } catch (int e) {
    S s;
    goto done;
  done:
    use();
  }
}

// The label must survive flattening and be a real goto target; the handler-local
// dtor and end-catch must NOT be emitted on the goto path (only at handler exit).

// CIR-LABEL: cir.func {{.*}} @_Z21goto_in_catch_handlerv
// CIR: catch [type #cir.global_view<@_ZTIi>
// CIR:   cir.catch_param
// CIR:   cir.goto "done"
// CIR: cir.label "done"
// CIR:   cir.call exception @_Z3usev()
// CIR:   cir.call @_ZN1SD1Ev
// CIR:   cir.catch_param end

// LLVM-LABEL: define {{.*}} @_Z21goto_in_catch_handlerv
// LLVM: call ptr @__cxa_begin_catch
// The goto target block runs use(); the S dtor + end-catch run at handler exit.
// LLVM: invoke void @_Z3usev()
// LLVM: call void @_ZN1SD1Ev
// LLVM: call void @__cxa_end_catch
// LLVM: ret void

// OGCG-LABEL: define {{.*}} @_Z21goto_in_catch_handlerv
// OGCG: call ptr @__cxa_begin_catch
// OGCG: br label %done
// OGCG: done:
// OGCG: invoke void @_Z3usev()
// OGCG: call void @_ZN1SD1Ev
// OGCG: call void @__cxa_end_catch
// OGCG: ret void
