// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that UnusedFunctionPointer vtable components (which occur in
// virtual inheritance when an intermediate class does not override a
// virtual method) emit null pointer entries matching OGCG behavior.

struct V {
    virtual int id() { return 0; }
    virtual ~V() = default;
};
struct A : virtual V { int id() override { return 1; } };
struct B : virtual V { };  // does not override id — creates UnusedFunctionPointer
struct D : A, B { };

void use() { D d; }

// B's construction vtable (_ZTC1D8_1B) has an UnusedFunctionPointer for id()
// because B does not override it. The slot should be null.

// CIR: cir.global {{.*}}linkonce_odr {{.*}} @_ZTC1D8_1B
// CIR-SAME: #cir.ptr<null> : !cir.ptr<!u8i>, #cir.global_view<@_ZN1BD1Ev>

// LLVM: @_ZTC1D8_1B = linkonce_odr {{.*}} ptr null, ptr @_ZN1BD1Ev
// OGCG: @_ZTC1D8_1B = linkonce_odr {{.*}} ptr null, ptr @_ZN1BD1Ev
