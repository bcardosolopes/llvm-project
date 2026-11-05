// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o - | FileCheck %s --check-prefix=OGCG
//
// Regression test: When a typed catch handler (catch(int)) appears before
// catch(...), the landing pad must include both `catch ptr @_ZTIi` and
// `catch ptr null`. Previously, the landing pad only included the typed
// catch, so unmatched exceptions fell through to std::terminate().

void thrower() {
    throw 3.14; // throws double
}

int test() {
    try {
        thrower();
    } catch (int e) {
        return 1; // not matched
    } catch (...) {
        return 2; // should catch here
    }
    return 0;
}

// CHECK: landingpad { ptr, i32 }
// CHECK-NEXT: catch ptr @_ZTIi
// CHECK-NEXT: catch ptr null

// OGCG: define {{.*}} @_Z7throwerv()
// OGCG:   call ptr @__cxa_allocate_exception
// OGCG:   call void @__cxa_throw
//
// OGCG: define {{.*}} @_Z4testv()
// OGCG:   invoke void @_Z7throwerv()
// OGCG: landingpad { ptr, i32 }
// OGCG-NEXT: catch ptr @_ZTIi
// OGCG-NEXT: catch ptr null
