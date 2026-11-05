// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s
//
// Verify noexcept void call emits matching invoke-to-terminate CFG.
//

void g();
void f() noexcept { g(); }

// CHECK: define {{.*}}void @_Z1fv()
// CHECK:   invoke void @_Z1gv()
// CHECK:     to label %[[CONT:.*]] unwind label %[[LPAD:.*]]
// CHECK: [[CONT]]:
// CHECK-NEXT: ret void
// CHECK: [[LPAD]]:
// CHECK-NEXT: %[[EXN:.*]] = landingpad { ptr, i32 }
// CHECK-NEXT:   catch ptr null
// CHECK-NEXT: %[[PTR:.*]] = extractvalue { ptr, i32 } %[[EXN]], 0
// CHECK-NEXT: call void @__clang_call_terminate(ptr %[[PTR]]) #[[NORETURN:.*]]
// CHECK-NEXT: unreachable
// CHECK: attributes #[[NORETURN]] = {{{.*}}noreturn{{.*}}nounwind{{.*}}}

// OGCG: define {{.*}}void @_Z1fv()
// OGCG:   invoke void @_Z1gv()
// OGCG:     to label %[[CONT:.*]] unwind label %[[LPAD:.*]]
// OGCG: [[CONT]]:
// OGCG-NEXT: ret void
// OGCG: [[LPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @__clang_call_terminate({{.*}}) #[[NORETURN:.*]]
// OGCG:   unreachable
// OGCG: attributes #[[NORETURN]] = {{{.*}}noreturn{{.*}}nounwind{{.*}}}
