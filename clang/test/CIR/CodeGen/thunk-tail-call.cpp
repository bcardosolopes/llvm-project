// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Multiple inheritance thunks use tail call and inbounds GEP

struct A { virtual void f(); int a; };
struct B { virtual void g(); int b; };
struct C : A, B {
    void f() override;
    void g() override;
};
void C::f() {}
void C::g() {}

// OGCG thunk uses tail call and inbounds:
// OGCG: define{{.*}} void @_ZThn16_N1C1gEv(ptr noundef %{{.*}})
// OGCG: getelementptr inbounds i8
// OGCG: tail call void @_ZN1C1gEv

// CIR thunk matches:
// CIR: define{{.*}} void @_ZThn16_N1C1gEv(ptr noundef %{{.*}})
// CIR: getelementptr inbounds i8
// CIR: tail call void @_ZN1C1gEv
