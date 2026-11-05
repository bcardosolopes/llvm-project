// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test thunk generation for virtual destructors in multiple inheritance.
// Both CIR and OGCG generate the D1/D0 thunks. Both now emit `getelementptr
// inbounds` for the this-pointer adjustment.

class Base1 {
public:
  virtual ~Base1() {}
  int x;
};

class Base2 {
public:
  virtual ~Base2() {}
  int y;
};

class Derived : public Base1, public Base2 {
public:
  ~Derived() override {}
};

void test() {
  Base2* b2 = new Derived();
  delete b2;  // Uses destructor thunk
}

// LLVM-LABEL: define linkonce_odr void @_ZThn16_N7DerivedD1Ev
// LLVM:         getelementptr inbounds i8, ptr %{{.*}}, i64 -16
// LLVM:         tail call void @_ZN7DerivedD1Ev

// LLVM-LABEL: define linkonce_odr void @_ZThn16_N7DerivedD0Ev
// LLVM:         getelementptr inbounds i8, ptr %{{.*}}, i64 -16
// LLVM:         tail call void @_ZN7DerivedD0Ev

// OGCG-LABEL: define linkonce_odr void @_ZThn16_N7DerivedD1Ev
// OGCG:         getelementptr inbounds i8, ptr %{{.*}}, i64 -16
// OGCG:         call void @_ZN7DerivedD1Ev

// OGCG-LABEL: define linkonce_odr void @_ZThn16_N7DerivedD0Ev
// OGCG:         getelementptr inbounds i8, ptr %{{.*}}, i64 -16
// OGCG:         call void @_ZN7DerivedD0Ev
