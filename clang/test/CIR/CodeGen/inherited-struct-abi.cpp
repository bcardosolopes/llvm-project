// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// Verify that structs inheriting all data from base classes have correct ABI
// lowering. Previously, such structs were incorrectly classified as empty
// (NoClass) because the empty-record check only examined direct fields.

// Single inheritance, 8 bytes — should be coerced to i64.
struct B8 { char data[8]; };
struct D8 : B8 {};

// CIR-LABEL: define {{.*}} @_Z2f82D8(i64 %
// OGCG-LABEL: define {{.*}} @_Z2f82D8(i64 %
D8 f8(D8 a) { return a; }

// Single inheritance, 16 bytes — should be coerced to {i64, i64}.
struct B16 { char data[16]; };
struct D16 : B16 {};

// CIR-LABEL: define {{.*}} @_Z3f163D16(i64 %{{.*}}, i64 %
// OGCG-LABEL: define {{.*}} @_Z3f163D16(i64 %{{.*}}, i64 %
D16 f16(D16 a) { return a; }

// Single inheritance, 24 bytes — should use sret/byval.
struct B24 { char data[24]; };
struct D24 : B24 {};

// CIR-LABEL: define {{.*}} @_Z3f243D24(ptr {{.*}} sret(%struct.D24) {{.*}}, ptr {{.*}} byval(%struct.D24)
// OGCG-LABEL: define {{.*}} @_Z3f243D24(ptr {{.*}} sret(%struct.D24) {{.*}}, ptr {{.*}} byval(%struct.D24)
D24 f24(D24 a) { return a; }

// Multiple inheritance — should use sret/byval.
struct M1 { char x[12]; };
struct M2 { char y[12]; };
struct DM : M1, M2 {};

// CIR-LABEL: define {{.*}} @_Z2fm2DM(ptr {{.*}} sret(%struct.DM) {{.*}}, ptr {{.*}} byval(%struct.DM)
// OGCG-LABEL: define {{.*}} @_Z2fm2DM(ptr {{.*}} sret(%struct.DM) {{.*}}, ptr {{.*}} byval(%struct.DM)
DM fm(DM a) { return a; }

// Truly empty struct (no bases with data) — should still be NoClass.
struct Empty {};
struct DEmpty : Empty {};

// CIR-LABEL: define {{.*}} @_Z2fe6DEmpty()
// OGCG-LABEL: define {{.*}} @_Z2fe6DEmpty()
DEmpty fe(DEmpty a) { return a; }

// --- Virtual inheritance ---
// Virtual bases add vtable pointers, changing the layout and ABI classification.

// Single virtual base with data — vtable pointer makes it >16 bytes, uses sret.
struct VBase { char data[8]; };
struct VDerived : virtual VBase {};

// CIR-LABEL: define {{.*}} @_Z14f_single_vbase8VDerived(ptr {{.*}} sret(%struct.VDerived) align 8 %{{.*}}, ptr {{.*}} %
// OGCG-LABEL: define {{.*}} @_Z14f_single_vbase8VDerived(ptr {{.*}} sret(%struct.VDerived) align 8 %{{.*}}, ptr {{.*}} %
VDerived f_single_vbase(VDerived a) { return a; }

// Diamond virtual inheritance — shared virtual base.
struct DiamondBase { char data[4]; };
struct DiamondLeft : virtual DiamondBase {};
struct DiamondRight : virtual DiamondBase {};
struct DiamondDerived : DiamondLeft, DiamondRight {};

// CIR-LABEL: define {{.*}} @_Z9f_diamond14DiamondDerived(ptr {{.*}} sret(%struct.DiamondDerived) align 8 %{{.*}}, ptr {{.*}} %
// OGCG-LABEL: define {{.*}} @_Z9f_diamond14DiamondDerived(ptr {{.*}} sret(%struct.DiamondDerived) align 8 %{{.*}}, ptr {{.*}} %
DiamondDerived f_diamond(DiamondDerived a) { return a; }

// Empty virtual base — still adds vtable pointer, uses sret.
struct EmptyVBase {};
struct EmptyVDerived : virtual EmptyVBase {};

// CIR-LABEL: define {{.*}} @_Z13f_empty_vbase13EmptyVDerived(ptr {{.*}} sret(%struct.EmptyVDerived) align 8 %{{.*}}, ptr {{.*}} %
// OGCG-LABEL: define {{.*}} @_Z13f_empty_vbase13EmptyVDerived(ptr {{.*}} sret(%struct.EmptyVDerived) align 8 %{{.*}}, ptr {{.*}} %
EmptyVDerived f_empty_vbase(EmptyVDerived a) { return a; }
