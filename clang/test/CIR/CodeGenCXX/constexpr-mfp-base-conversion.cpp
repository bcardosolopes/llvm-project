// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Constexpr member function pointers with base class conversion
// were lowered to zeroinitializer instead of { ptrtoint(fn), adjustment }.

namespace MemberPtr {
  struct B1 {
    int a, b;
    virtual void f();
    void g();
  };
  struct B2 {
    int c, d;
    virtual void h();
    void i();
  };
  struct C : B1 {
    int e;
    virtual void j();
    void k();
  };
  struct D : C, B2 {
    int z;
    virtual void l();
    void m();
  };

  // Virtual, no conversion — { vtable_offset + 1, 0 }
  extern constexpr void (D::*df)() = &C::f;
  // CHECK: @_ZN9MemberPtr2dfE ={{.*}} constant {{.*}} { i64 1, i64 0 }
  // OGCG: @_ZN9MemberPtr2dfE ={{.*}} constant {{.*}} { i64 1, i64 0 }

  // Non-virtual, base conversion B1->D — { ptrtoint(@g), 0 }
  extern constexpr void (D::*dg)() = &B1::g;
  // CHECK: @_ZN9MemberPtr2dgE ={{.*}} constant {{.*}} { i64 ptrtoint (ptr @_ZN9MemberPtr2B11gEv to i64), i64 0 }
  // OGCG: @_ZN9MemberPtr2dgE ={{.*}} constant {{.*}} { i64 ptrtoint (ptr @_ZN9MemberPtr2B11gEv to i64), i64 0 }

  // Non-virtual, base conversion B2->D — { ptrtoint(@i), 24 }
  // B2 is at offset 24 in D (C has B1's vptr + a, b, e = 8+4+4+4 = 20, padded to 24)
  extern constexpr void (D::*di)() = &D::i;
  // CHECK: @_ZN9MemberPtr2diE ={{.*}} constant {{.*}} { i64 ptrtoint (ptr @_ZN9MemberPtr2B21iEv to i64), i64 24 }
  // OGCG: @_ZN9MemberPtr2diE ={{.*}} constant {{.*}} { i64 ptrtoint (ptr @_ZN9MemberPtr2B21iEv to i64), i64 24 }

  // Non-virtual, base conversion C->D (trivial, offset 0) — { ptrtoint(@k), 0 }
  extern constexpr void (D::*dk)() = &C::k;
  // CHECK: @_ZN9MemberPtr2dkE ={{.*}} constant {{.*}} { i64 ptrtoint (ptr @_ZN9MemberPtr1C1kEv to i64), i64 0 }
  // OGCG: @_ZN9MemberPtr2dkE ={{.*}} constant {{.*}} { i64 ptrtoint (ptr @_ZN9MemberPtr1C1kEv to i64), i64 0 }

  // Downcast: D::m as B2 member function — { ptrtoint(@m), -24 }
  extern constexpr void (B2::*b2m)() = (void(B2::*)())&D::m;
  // CHECK: @_ZN9MemberPtr3b2mE ={{.*}} constant {{.*}} { i64 ptrtoint (ptr @_ZN9MemberPtr1D1mEv to i64), i64 -24 }
  // OGCG: @_ZN9MemberPtr3b2mE ={{.*}} constant {{.*}} { i64 ptrtoint (ptr @_ZN9MemberPtr1D1mEv to i64), i64 -24 }
}
