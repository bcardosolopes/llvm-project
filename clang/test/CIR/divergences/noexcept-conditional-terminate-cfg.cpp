// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s
//
// Verify conditional noexcept emits a single shared terminate landing pad.
//

void g();
void h();
void f(bool b) noexcept {
  if (b)
    g();
  else
    h();
}

// CHECK: define {{.*}}void @_Z1fb(i1 noundef zeroext %0)
// CHECK:   br i1 %{{.*}}, label %[[THEN:.*]], label %[[ELSE:.*]]
// CHECK: [[THEN]]:
// CHECK:   invoke void @_Z1gv()
// CHECK:     to label %{{.*}} unwind label %[[LPAD:.*]]
// CHECK: [[LPAD]]:
// CHECK-NEXT: %[[EXN:.*]] = landingpad { ptr, i32 }
// CHECK-NEXT:   catch ptr null
// CHECK-NEXT: %[[PTR:.*]] = extractvalue { ptr, i32 } %[[EXN]], 0
// CHECK-NEXT: call void @__clang_call_terminate(ptr %[[PTR]]) #[[NORETURN:.*]]
// CHECK-NEXT: unreachable
// CHECK: [[ELSE]]:
// CHECK:   invoke void @_Z1hv()
// CHECK:     to label %{{.*}} unwind label %[[LPAD]]
// CHECK: attributes #[[NORETURN]] = {{{.*}}noreturn{{.*}}nounwind{{.*}}}

// OGCG: define {{.*}}void @_Z1fb(i1 noundef zeroext %b)
// OGCG:   br i1 %{{.*}}, label %[[THEN:.*]], label %[[ELSE:.*]]
// OGCG: [[THEN]]:
// OGCG:   invoke void @_Z1gv()
// OGCG:     to label %{{.*}} unwind label %[[LPAD:.*]]
// OGCG: [[ELSE]]:
// OGCG:   invoke void @_Z1hv()
// OGCG:     to label %{{.*}} unwind label %[[LPAD]]
// OGCG: [[LPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @__clang_call_terminate({{.*}}) #[[NORETURN:.*]]
// OGCG:   unreachable
// OGCG: attributes #[[NORETURN]] = {{{.*}}noreturn{{.*}}nounwind{{.*}}}
