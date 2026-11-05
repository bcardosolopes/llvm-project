// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - -fcxx-exceptions -fexceptions | FileCheck %s

struct E { int code; };
bool a();
bool b();
bool c();
void h();

void test_if_shortcircuit_catch_decl() {
  if (a() && b() && c()) {
    try {
      h();
    } catch (const E &e) {
      h();
    }
  }
}

// CHECK-LABEL: @_Z31test_if_shortcircuit_catch_declv
// CHECK: cir.if
// CHECK: cir.try
// CHECK: cir.catch_param
// CHECK: cir.store align(8) {{.*}} : !cir.ptr<!rec_E>, !cir.ptr<!cir.ptr<!rec_E>>
