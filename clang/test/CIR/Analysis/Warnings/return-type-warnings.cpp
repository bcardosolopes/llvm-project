// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -Wreturn-type -Wunused-variable %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=CFG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Wreturn-type -Wunused-variable %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=CIR

// Without a ClangIR pipeline to issue them, the flag leaves the warnings to
// the CFG check.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsyntax-only -fclangir-analysis-warnings=return-type -Wreturn-type -Wunused-variable %s 2>&1 | FileCheck %s --check-prefix=CFG

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Werror=return-type %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERROR

// The CFG check reports each function as its body is finished. ClangIR
// reports after the translation unit is generated, so the unused variable
// below comes first.

int maybe(int x) {
  if (x)
    return 1;
}

void unused() { int u; }

template <typename T> T instantiated(T x) {
  if (x)
    return x;
}
int use() { return instantiated(1); }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
int ignored(int x) {
  if (x)
    return 1;
}
#pragma clang diagnostic pop

// CFG: return-type-warnings.cpp:17:1: warning: non-void function does not return a value in all control paths
// CFG: return-type-warnings.cpp:19:21: warning: unused variable 'u'
// CFG: return-type-warnings.cpp:24:1: warning: non-void function does not return a value in all control paths
// CFG: return-type-warnings.cpp:25:20: note: in instantiation of function template specialization 'instantiated<int>' requested here
// CFG-NOT: warning:

// CIR: return-type-warnings.cpp:19:21: warning: unused variable 'u'
// CIR: return-type-warnings.cpp:17:1: warning: non-void function does not return a value in all control paths
// CIR: return-type-warnings.cpp:24:1: warning: non-void function does not return a value in all control paths
// CIR: return-type-warnings.cpp:25:20: note: in instantiation of function template specialization 'instantiated<int>' requested here
// CIR-NOT: warning:

// ERROR: return-type-warnings.cpp:17:1: error: non-void function does not return a value in all control paths
// ERROR: return-type-warnings.cpp:24:1: error: non-void function does not return a value in all control paths

// Unlike the CFG, ClangIR does not build IR for code with errors, so it does
// not analyze it.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Wreturn-type -DSEMA_ERROR %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=SEMA-ERROR
#ifdef SEMA_ERROR
int sema_error = undeclared;
#endif

// SEMA-ERROR-NOT: does not return a value
// SEMA-ERROR: error: use of undeclared identifier 'undeclared'
// SEMA-ERROR-NOT: does not return a value

// TODO(cir): ClangIR reports only the innermost instantiation note.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsyntax-only -Wreturn-type -DNESTED %s 2>&1 | FileCheck %s --check-prefix=NESTED-CFG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Wreturn-type -DNESTED %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NESTED-CIR
#ifdef NESTED
template <typename T> T inner(T x) {
  if (x)
    return x;
}
template <typename T> T outer(T x) { return inner(x); }
int use_nested() { return outer(1); }
#endif

// NESTED-CFG: note: in instantiation of function template specialization 'inner<int>' requested here
// NESTED-CFG: note: in instantiation of function template specialization 'outer<int>' requested here
// NESTED-CIR: note: in instantiation of function template specialization 'inner<int>' requested here
// NESTED-CIR-NOT: note:
