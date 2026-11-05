// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.og.ll %s

// Verify that covariant return thunks null-check the return value before
// applying the byte-offset adjustment. Applying an offset to a null pointer
// is undefined behavior. The thunk must return null unchanged.

struct Base1 {
    virtual ~Base1() = default;
    virtual Base1* clone() { return this; }
};

struct Base2 {
    virtual ~Base2() = default;
    int x = 0;
};

// Derived inherits Base2 first, so the Base1 subobject is at a non-zero
// offset. The covariant return thunk for clone() must adjust the pointer
// by +16 bytes (Base1 offset in Derived), but only if the result is non-null.
struct Derived : Base2, Base1 {
    Derived* clone() override { return this; }
};

// Constructing a Derived forces vtable emission, including the covariant
// return thunk _ZTchn16_h16_N7Derived5cloneEv.
void use() {
    Derived d;
}

// The covariant thunk adjusts 'this' by -16 to call Derived::clone(),
// then adjusts the return value by +16 to convert Derived* -> Base1*.
// It must null-check the return value before the +16 adjustment.

// CIR-LABEL: @_ZTchn16_h16_N7Derived5cloneEv
// CIR:         cir.call @_ZN7Derived5cloneEv
// CIR:         %[[NULL_CMP:.*]] = cir.cmp(eq,
// CIR:         cir.ternary(%[[NULL_CMP]], true {
// CIR:           cir.yield
// CIR:         }, false {
// CIR:           cir.ptr_stride inbounds
// CIR:           cir.yield
// CIR:         })

// LLVM-LABEL: @_ZTchn16_h16_N7Derived5cloneEv
// LLVM:         %[[CALL:.*]] = call noundef ptr @_ZN7Derived5cloneEv
// LLVM:         %[[NULL:.*]] = icmp eq ptr %[[CALL]], null
// LLVM:         br i1 %[[NULL]]
// LLVM:         getelementptr inbounds i8, ptr %[[CALL]], i64 16
// LLVM:         phi ptr

// OGCG-LABEL: @_ZTchn16_h16_N7Derived5cloneEv
// OGCG:         call {{.*}} ptr @_ZN7Derived5cloneEv
// OGCG:         icmp eq ptr %{{.*}}, null
// OGCG:         getelementptr inbounds i8, ptr %{{.*}}, i64 16
// OGCG:         phi ptr
