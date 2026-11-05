// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Verify that sret alignment is correct for classes with vtable pointers
// and inheritance. The vtable pointer (8 bytes on x86_64) should enforce
// alignment 8 on the entire struct.

struct Base { int x; virtual void f() {} };

// Class with vtable + int member (simplest vtable case)
struct VTint { virtual void g() {} int x; };
VTint make_vtint() { return VTint{}; }
// LLVM: define dso_local void @_Z10make_vtintv(ptr dead_on_unwind noalias writable sret(%struct.VTint) align 8
// OGCG: define dso_local void @_Z10make_vtintv(ptr dead_on_unwind noalias writable sret(%struct.VTint) align 8

// Non-virtual inheritance from class with vtable
struct NonVirt : Base { int y; };
NonVirt make_nv() { NonVirt n; return n; }
// LLVM: define dso_local void @_Z7make_nvv(ptr dead_on_unwind noalias writable sret(%struct.NonVirt) align 8
// OGCG: define dso_local void @_Z7make_nvv(ptr dead_on_unwind noalias writable sret(%struct.NonVirt) align 8

// Virtual inheritance
struct Virt : virtual Base { int y; };
Virt make_v() { Virt v; return v; }
// LLVM: define dso_local void @_Z6make_vv(ptr dead_on_unwind noalias writable sret(%struct.Virt) align 8
// OGCG: define dso_local void @_Z6make_vv(ptr dead_on_unwind noalias writable sret(%struct.Virt) align 8

// Deep inheritance chain
struct GrandChild : NonVirt { int z; };
GrandChild make_gc() { GrandChild g; return g; }
// LLVM: define dso_local void @_Z7make_gcv(ptr dead_on_unwind noalias writable sret(%struct.GrandChild) align 8
// OGCG: define dso_local void @_Z7make_gcv(ptr dead_on_unwind noalias writable sret(%struct.GrandChild) align 8

// Multiple inheritance
struct Base2 { int a; virtual void h() {} };
struct Multi : Base, Base2 { int m; };
Multi make_multi() { Multi m; return m; }
// LLVM: define dso_local void @_Z10make_multiv(ptr dead_on_unwind noalias writable sret(%struct.Multi) align 8
// OGCG: define dso_local void @_Z10make_multiv(ptr dead_on_unwind noalias writable sret(%struct.Multi) align 8

// Abstract base
struct Abstract { virtual void pure() = 0; int x; };
struct Concrete : Abstract { void pure() override {} int y; };
Concrete make_concrete() { Concrete c; return c; }
// LLVM: define dso_local void @_Z13make_concretev(ptr dead_on_unwind noalias writable sret(%struct.Concrete) align 8
// OGCG: define dso_local void @_Z13make_concretev(ptr dead_on_unwind noalias writable sret(%struct.Concrete) align 8
