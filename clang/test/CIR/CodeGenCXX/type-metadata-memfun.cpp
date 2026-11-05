// RUN: %clang_cc1 -flto -flto-unit -triple x86_64-unknown-linux -fvisibility=hidden -fclangir -emit-llvm -o - %s | FileCheck %s

struct S1 {
  S1();
  ~S1();
  virtual void vf();
  void f();
  void fdecl();
};

struct [[clang::lto_visibility_public]] S2 {
  void f();
};

S1::S1() {}
S1::~S1() {}
void S1::vf() {}

// CHECK: define hidden void @_ZN2S11fEv(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}){{.*}} !type [[S1F:![0-9]+]]
void S1::f() {
  fdecl();
}

void S2::f() {}

// CHECK: [[S1F]] = !{i64 0, !"_ZTSM2S1FvvE"}
