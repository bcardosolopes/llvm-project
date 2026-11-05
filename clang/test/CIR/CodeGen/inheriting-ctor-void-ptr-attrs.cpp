// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: inheriting constructors with virtual bases must not
// apply reference-parameter attributes (nonnull, align, dereferenceable)
// to void* parameters. The bug was that setFunctionAttributes always
// checked needsVTTParameter with Ctor_Base, causing a VTT offset to be
// applied even for CI1 (complete object) constructors that don't have a
// VTT parameter, misaligning the reference attribute to the void* arg.

struct Q { operator int(); };
struct Z { Z(); };
namespace noninline_virt {
  struct A { A(int, Q&&, void *__attribute__((pass_object_size(0)))); int n; };
  struct B : Z, virtual A { Z z; using A::A; };
  Q q;
  B b(1, static_cast<Q&&>(q), &b);
}

// Check the CI1 (complete object) inheriting constructor:
// - arg 0 (this): nonnull align 8 dereferenceable(9)
// - arg 1 (int):  noundef only
// - arg 2 (Q&&):  nonnull align 1 dereferenceable(1)
// - arg 3 (void*): noundef only — must NOT have nonnull/align/dereferenceable
// - arg 4 (i64 from pass_object_size): noundef only

// LLVM: define {{.*}} @_ZN14noninline_virt1BCI1NS_1AEEiO1QPvU17pass_object_size0(
// LLVM-SAME: ptr noundef nonnull align 8 dereferenceable(9)
// LLVM-SAME: i32 noundef
// LLVM-SAME: ptr noundef nonnull align 1 dereferenceable(1)
// LLVM-SAME: ptr noundef %
// LLVM-SAME: i64 noundef

// OGCG: define {{.*}} @_ZN14noninline_virt1BCI1NS_1AEEiO1QPvU17pass_object_size0(
// OGCG-SAME: ptr noundef nonnull align 8 dereferenceable(9)
// OGCG-SAME: i32 noundef
// OGCG-SAME: ptr noundef nonnull align 1 dereferenceable(1)
// OGCG-SAME: ptr noundef %
// OGCG-SAME: i64 noundef
