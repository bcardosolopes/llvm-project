// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden -fsanitize=cfi-vcall -fsanitize-trap=cfi-vcall -fclangir -emit-llvm -o - %s | FileCheck %s

struct A {
  virtual void f(int);
};

int g();
void f(A *a) {
  // CHECK: call noundef i32 @_Z1gv()
  // CHECK-NEXT: [[VTABLE:%.*]] = load ptr, ptr {{%.*}}, align 8
  // CHECK-NEXT: [[CHECK:%.*]] = call i1 @llvm.type.test(ptr [[VTABLE]], metadata !"_ZTS1A")
  a->f(g());
}
