// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Tests for EH cleanup ordering in catch handlers, comparing CIR and OGCG.

struct A { A(); ~A(); };
struct B { B(); ~B(); };
void may_throw();
void use(int);

// Multiple RAII objects in catch handler with return.
// Cleanup order must be: ~B, ~A, __cxa_end_catch.
int multi_raii_catch_return() {
  try {
    may_throw();
  } catch (int e) {
    A a;
    B b;
    return e;
  }
  return 0;
}

// LLVM-LABEL: define {{.*}} @_Z23multi_raii_catch_returnv
// LLVM: personality ptr @__gxx_personality_v0
// LLVM: invoke void @_Z9may_throwv()
// LLVM: landingpad
// LLVM:   catch ptr @_ZTIi
// LLVM: call ptr @__cxa_begin_catch
// LLVM: call void @_ZN1BD1Ev
// LLVM: call void @_ZN1AD1Ev
// LLVM: call void @__cxa_end_catch
// LLVM: ret i32

// OGCG-LABEL: define {{.*}} @_Z23multi_raii_catch_returnv
// OGCG: personality ptr @__gxx_personality_v0
// OGCG: invoke void @_Z9may_throwv()
// OGCG: landingpad
// OGCG:   catch ptr @_ZTIi
// OGCG: call ptr @__cxa_begin_catch
// OGCG: call void @_ZN1BD1Ev
// OGCG: call void @_ZN1AD1Ev
// OGCG: call void @__cxa_end_catch

// Nested try-catch where inner catch rethrows, outer catch returns.
// Must have two landingpads and proper rethrow semantics.
int nested_catch_rethrow_return() {
  try {
    try {
      may_throw();
    } catch (...) {
      throw;
    }
  } catch (int e) {
    return e;
  }
  return 0;
}

// LLVM-LABEL: define {{.*}} @_Z27nested_catch_rethrow_returnv
// LLVM: invoke void @_Z9may_throwv()
// LLVM: landingpad
// LLVM: call ptr @__cxa_begin_catch
// LLVM: invoke void @__cxa_rethrow()
// LLVM: landingpad
// LLVM: invoke void @__cxa_end_catch
// LLVM: call ptr @__cxa_begin_catch

// OGCG-LABEL: define {{.*}} @_Z27nested_catch_rethrow_returnv
// OGCG: invoke void @_Z9may_throwv()
// OGCG: landingpad
// OGCG: call ptr @__cxa_begin_catch
// OGCG: invoke void @__cxa_rethrow()
// OGCG: landingpad
// OGCG: invoke void @__cxa_end_catch
// OGCG: call ptr @__cxa_begin_catch

// RAII in both try body and catch handler with return.
// ~A must run on throw path (landing pad cleanup), ~B on catch return path.
int raii_both_try_and_catch() {
  try {
    A a;
    may_throw();
  } catch (int e) {
    B b;
    return e;
  }
  return 0;
}

// LLVM-LABEL: define {{.*}} @_Z23raii_both_try_and_catchv
// LLVM: invoke void @_Z9may_throwv()
// LLVM: landingpad
// LLVM:   catch ptr @_ZTIi
// LLVM: call void @_ZN1AD1Ev
// LLVM: call ptr @__cxa_begin_catch
// LLVM: call void @_ZN1BD1Ev
// LLVM: call void @__cxa_end_catch

// OGCG-LABEL: define {{.*}} @_Z23raii_both_try_and_catchv
// OGCG: invoke void @_Z9may_throwv()
// OGCG: landingpad
// OGCG:   catch ptr @_ZTIi
// OGCG: call void @_ZN1AD1Ev
// OGCG: call ptr @__cxa_begin_catch
// OGCG: call void @_ZN1BD1Ev
// OGCG: call void @__cxa_end_catch

// Break from catch handler inside a for loop.
// __cxa_end_catch must be called before breaking out of the loop.
int break_from_catch_in_loop() {
  int result = 0;
  for (int i = 0; i < 10; ++i) {
    try {
      may_throw();
    } catch (int e) {
      result = e;
      break;
    }
  }
  return result;
}

// LLVM-LABEL: define {{.*}} @_Z24break_from_catch_in_loopv
// LLVM: invoke void @_Z9may_throwv()
// LLVM: landingpad
// LLVM:   catch ptr @_ZTIi
// LLVM: call ptr @__cxa_begin_catch
// LLVM: __cxa_end_catch

// OGCG-LABEL: define {{.*}} @_Z24break_from_catch_in_loopv
// OGCG: invoke void @_Z9may_throwv()
// OGCG: landingpad
// OGCG:   catch ptr @_ZTIi
// OGCG: call ptr @__cxa_begin_catch
// OGCG: __cxa_end_catch
