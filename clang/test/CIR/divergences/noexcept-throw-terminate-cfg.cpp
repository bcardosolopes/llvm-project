// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s
//
// Verify noexcept throw emits matching invoke-to-terminate CFG.
//

void f() noexcept { throw 1; }

// CHECK: define {{.*}}void @_Z1fv()
// CHECK:   call {{.*}} @__cxa_allocate_exception(i64 4)
// CHECK:   store i32 1
// CHECK:   invoke void @__cxa_throw(
// CHECK:     to label %[[NORM:.*]] unwind label %[[LPAD:.*]]
// CHECK: [[LPAD]]:
// CHECK-NEXT: %[[EXN:.*]] = landingpad { ptr, i32 }
// CHECK-NEXT:   catch ptr null
// CHECK-NEXT: %[[PTR:.*]] = extractvalue { ptr, i32 } %[[EXN]], 0
// CHECK-NEXT: call void @__clang_call_terminate(ptr %[[PTR]]) #[[NORETURN:.*]]
// CHECK-NEXT: unreachable
// CHECK: attributes #[[NORETURN]] = {{{.*}}noreturn{{.*}}nounwind{{.*}}}

// OGCG: define {{.*}}void @_Z1fv()
// OGCG:   call {{.*}} @__cxa_allocate_exception(i64 4)
// OGCG:   store i32 1
// OGCG:   invoke void @__cxa_throw(
// OGCG:     to label %[[NORM:.*]] unwind label %[[LPAD:.*]]
// OGCG: [[LPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @__clang_call_terminate({{.*}}) #[[NORETURN:.*]]
// OGCG:   unreachable
// OGCG: attributes #[[NORETURN]] = {{{.*}}noreturn{{.*}}nounwind{{.*}}}
