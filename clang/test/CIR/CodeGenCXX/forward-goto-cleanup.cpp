// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Test that forward gotos through cleanup scopes correctly emit destructors.

struct RAII {
  int *p;
  RAII(int *p) : p(p) { *p = 1; }
  ~RAII() { *p = 0; }
};

// Forward goto exiting a scope — destructor must run before reaching label.
int forward_goto_exit_scope(int *flag) {
  {
    RAII r(flag);
    goto after;
  }
after:
  return *flag;
}

// Both CIR and OGCG must: construct, then destroy before reaching ret.
// CIR-LABEL: define {{.*}}i32 @_Z23forward_goto_exit_scopePi
// CIR: call void @_ZN4RAIIC1EPi
// CIR: call void @_ZN4RAIID1Ev
// CIR: ret i32

// OGCG-LABEL: define {{.*}}i32 @_Z23forward_goto_exit_scopePi
// OGCG: call void @_ZN4RAIIC1EPi
// OGCG: call void @_ZN4RAIID1Ev
// OGCG: ret i32

// Forward goto skipping nested scopes — both destructors must run.
int forward_goto_nested_scopes(int *a, int *b) {
  {
    RAII ra(a);
    {
      RAII rb(b);
      goto done;
    }
  }
done:
  return *a + *b;
}

// Both CIR and OGCG must: construct ra, construct rb, destroy rb, destroy ra.
// CIR-LABEL: define {{.*}}i32 @_Z26forward_goto_nested_scopesPiS_
// CIR: @_ZN4RAIIC1EPi
// CIR: @_ZN4RAIIC1EPi
// CIR: call void @_ZN4RAIID1Ev
// CIR: call void @_ZN4RAIID1Ev
// CIR: ret i32

// OGCG-LABEL: define {{.*}}i32 @_Z26forward_goto_nested_scopesPiS_
// OGCG: @_ZN4RAIIC1EPi
// OGCG: @_ZN4RAIIC1EPi
// OGCG: call void @_ZN4RAIID1Ev
// OGCG: call void @_ZN4RAIID1Ev
// OGCG: ret i32

// Forward goto through a macro — tests that source-range heuristic works
// correctly with macro-expanded goto statements.
#define JUMP_TO(label) goto label

int forward_goto_via_macro(int *flag) {
  {
    RAII r(flag);
    JUMP_TO(target);
  }
target:
  return *flag;
}

// CIR-LABEL: define {{.*}}i32 @_Z22forward_goto_via_macroPi
// CIR: call void @_ZN4RAIIC1EPi
// CIR: call void @_ZN4RAIID1Ev
// CIR: ret i32

// OGCG-LABEL: define {{.*}}i32 @_Z22forward_goto_via_macroPi
// OGCG: call void @_ZN4RAIIC1EPi
// OGCG: call void @_ZN4RAIID1Ev
// OGCG: ret i32
