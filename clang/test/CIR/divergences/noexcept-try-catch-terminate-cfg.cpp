// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s
//
// Verify noexcept try/catch emits invoke-to-terminate for __cxa_end_catch,
// matching OGCG behavior. (previously)

void g();
void f() noexcept {
  try {
    g();
  } catch (...) {
  }
}

// CHECK: define {{.*}}void @_Z1fv()
// CHECK:   invoke void @_Z1gv()
// CHECK:     to label %[[NORMAL:.*]] unwind label %[[LPAD:.*]]
// CHECK: [[CATCH:.*]]:
// CHECK:   call ptr @__cxa_begin_catch({{.*}})
// CHECK:   invoke void @__cxa_end_catch()
// CHECK:     to label %[[CONT:.*]] unwind label %[[TERMLPAD:.*]]
// Verify single-block terminate landing pad (no forwarding block).
// CHECK: [[TERMLPAD]]:
// CHECK-NEXT: %[[EXN:.*]] = landingpad { ptr, i32 }
// CHECK-NEXT:   catch ptr null
// CHECK-NEXT: %[[SLOT:.*]] = extractvalue { ptr, i32 } %[[EXN]], 0
// CHECK-NEXT: call void @__clang_call_terminate(ptr %[[SLOT]]) #[[NORETURN:.*]]
// CHECK-NEXT: unreachable
// CHECK: attributes #[[NORETURN]] = {{{.*}}noreturn{{.*}}nounwind{{.*}}}

// OGCG: define {{.*}}void @_Z1fv()
// OGCG:   invoke void @_Z1gv()
// OGCG:     to label %[[NORMAL:.*]] unwind label %[[LPAD:.*]]
// OGCG: [[CATCH:.*]]:
// OGCG:   call ptr @__cxa_begin_catch({{.*}})
// OGCG:   invoke void @__cxa_end_catch()
// OGCG:     to label %[[CONT:.*]] unwind label %[[TERMLPAD:.*]]
// OGCG: [[TERMLPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @__clang_call_terminate({{.*}}) #[[NORETURN:.*]]
// OGCG:   unreachable
// OGCG: attributes #[[NORETURN]] = {{{.*}}noreturn{{.*}}nounwind{{.*}}}
