// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// A `goto` inside a catch handler that jumps to a label in the else branch of
// an `if` with a condition variable stays within the condition variable's
// scope: the label is lexically inside the if-statement. The condition
// variable must therefore be destroyed exactly once on the goto path (at the
// merged end of the if-statement scope), NOT a second time at the point of the
// goto. This mirrors googletest's GTEST_TEST_THROW_ macro, where a spurious
// second destructor on the goto path double-frees gtest_msg's std::string
// member (heap corruption). See CIRGenStmt.cpp emitIfStmt.

struct S {
  int value;
  S();
  ~S();
  explicit operator bool() const;
};

void may_throw();

void run() {
  if (S gtest_msg{}) {
    try {
      may_throw();
    } catch (...) {
      goto label;
    }
  } else
    label:
      (void)gtest_msg.value;
}

// The catch handler must call __cxa_begin_catch / __cxa_end_catch and then
// branch to the label WITHOUT destroying gtest_msg. The single normal-path
// destructor of gtest_msg happens once at the merged if-statement scope exit,
// right before the function returns.

// LLVM-LABEL: define {{.*}} @_Z3runv(
// LLVM: call ptr @__cxa_begin_catch(
// LLVM: invoke void @__cxa_end_catch()
// LLVM-NOT: call void @_ZN1SD1Ev(
// LLVM: call void @_ZN1SD1Ev(
// LLVM-NEXT: ret void

// OGCG-LABEL: define {{.*}} @_Z3runv(
// OGCG: call ptr @__cxa_begin_catch(
// OGCG: invoke void @__cxa_end_catch()
// OGCG-NOT: call void @_ZN1SD1Ev(
// OGCG: call void @_ZN1SD1Ev(
// OGCG-NEXT: ret void
