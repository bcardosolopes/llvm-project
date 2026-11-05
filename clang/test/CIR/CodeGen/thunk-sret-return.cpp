// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify secondary vtable thunks with sret return.
// The thunk must forward the caller's sret pointer to the inner call.

struct Big { int data[5]; };

struct A { virtual ~A() = default; virtual void f() {} };
struct B { virtual ~B() = default; virtual Big get() = 0; };
struct C : A, B {
    void f() override {}
    Big get() override { return {{1,2,3,4,5}}; }
};

// The thunk for B::get() through C's secondary vtable should:
// 1. Forward the sret pointer directly to C::get()
// 2. Not trap or hit unreachable
// CHECK: define {{.*}} void @_ZThn8_N1C3getEv(ptr {{.*}} sret(%struct.Big) {{.*}} %[[SRET:.*]], ptr noundef %[[THIS:.*]])
// CHECK:   %[[ADJ:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 -8
// CHECK:   tail call void @_ZN1C3getEv(ptr {{.*}} sret(%struct.Big) {{.*}} %[[SRET]], ptr noundef nonnull align 8 dereferenceable(16) %[[ADJ]])
// CHECK:   ret void
void test() {
    C c;
    B* bp = &c;
    Big b = bp->get();
}
