// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// Tests AArch64 AAPCS64 Homogeneous Floating-point Aggregate (HFA) handling.
// HFAs with 1-4 members of the same float type are passed/returned in FP regs.

// HFA2: two floats — returned as %struct.HFA2, passed as [2 x float]
struct HFA2 { float a, b; };

// CIR-LABEL: define dso_local %struct.HFA2 @return_hfa2()
// OGCG-LABEL: define dso_local %struct.HFA2 @return_hfa2()
struct HFA2 return_hfa2(void) { return (struct HFA2){1.0f, 2.0f}; }

// CIR-LABEL: define dso_local void @take_hfa2([2 x float] alignstack(8) %
// OGCG-LABEL: define dso_local void @take_hfa2([2 x float] alignstack(8) %
void take_hfa2(struct HFA2 h) { (void)h.a; }

// HFA3D: three doubles — returned as %struct.HFA3D, passed as [3 x double]
struct HFA3D { double a, b, c; };

// CIR-LABEL: define dso_local %struct.HFA3D @return_hfa3d()
// OGCG-LABEL: define dso_local %struct.HFA3D @return_hfa3d()
struct HFA3D return_hfa3d(void) { return (struct HFA3D){1.0, 2.0, 3.0}; }

// CIR-LABEL: define dso_local void @take_hfa3d([3 x double] alignstack(8) %
// OGCG-LABEL: define dso_local void @take_hfa3d([3 x double] alignstack(8) %
void take_hfa3d(struct HFA3D h) { (void)h.a; }

// HFA4D: four doubles (maximum) — returned directly, passed as [4 x double]
struct HFA4D { double a, b, c, d; };

// CIR-LABEL: define dso_local %struct.HFA4D @return_hfa4d()
// OGCG-LABEL: define dso_local %struct.HFA4D @return_hfa4d()
struct HFA4D return_hfa4d(void) { return (struct HFA4D){1.0, 2.0, 3.0, 4.0}; }

// CIR-LABEL: define dso_local void @take_hfa4d([4 x double] alignstack(8) %
// OGCG-LABEL: define dso_local void @take_hfa4d([4 x double] alignstack(8) %
void take_hfa4d(struct HFA4D h) { (void)h.a; }

// NotHFA: 5 doubles exceeds HFA limit — returned via sret
struct NotHFA5D { double a, b, c, d, e; };

// CIR-LABEL: define dso_local void @return_not_hfa5d(ptr dead_on_unwind noalias writable sret(%struct.NotHFA5D) align 8 %
// OGCG-LABEL: define dso_local void @return_not_hfa5d(ptr dead_on_unwind noalias writable sret(%struct.NotHFA5D) align 8 %
struct NotHFA5D return_not_hfa5d(void) { return (struct NotHFA5D){1,2,3,4,5}; }

// Nested HFA: inner struct with 2 floats + outer adding 2 more = 4 floats HFA
struct Inner2F { float x, y; };
struct NestedHFA { struct Inner2F a, b; };

// CIR-LABEL: define dso_local %struct.NestedHFA @return_nested_hfa()
// OGCG-LABEL: define dso_local %struct.NestedHFA @return_nested_hfa()
struct NestedHFA return_nested_hfa(void) { return (struct NestedHFA){{1,2},{3,4}}; }

// CIR-LABEL: define dso_local void @take_nested_hfa([4 x float] alignstack(8) %
// OGCG-LABEL: define dso_local void @take_nested_hfa([4 x float] alignstack(8) %
void take_nested_hfa(struct NestedHFA h) { (void)h.a.x; }

// Mixed int/float — NOT an HFA, passed as [1 x i64]
struct MixedIF { int i; float f; };

// CIR-LABEL: define dso_local i64 @return_mixed_if()
// OGCG-LABEL: define dso_local i64 @return_mixed_if()
struct MixedIF return_mixed_if(void) { return (struct MixedIF){42, 3.14f}; }

// HFA with float array — array of 3 floats is an HFA
struct HFAArr { float arr[3]; };

// CIR-LABEL: define dso_local %struct.HFAArr @return_hfa_arr()
// OGCG-LABEL: define dso_local %struct.HFAArr @return_hfa_arr()
struct HFAArr return_hfa_arr(void) { struct HFAArr h = {{1,2,3}}; return h; }

// CIR-LABEL: define dso_local void @take_hfa_arr([3 x float] alignstack(8) %
// OGCG-LABEL: define dso_local void @take_hfa_arr([3 x float] alignstack(8) %
void take_hfa_arr(struct HFAArr h) { (void)h.arr[0]; }

// Union HFA: a union of an HFA struct and an equal-size float array is itself an
// HFA, returned in FP registers. Storing the returned union must NOT decompose
// it member-by-member (cir.extract_member is invalid on unions, since members
// overlap) — the coerced store falls back to a whole-value store.
struct S4F { float x, y, z, w; };
union UnionHFA { struct S4F s; float arr[4]; };
union UnionHFA guhfa;
union UnionHFA return_union_hfa(void);

// CIR-LABEL: define dso_local void @store_union_hfa()
// CIR:         %[[CALL:.*]] = call %union.UnionHFA @return_union_hfa()
// CIR:         store %union.UnionHFA %[[CALL]], ptr %{{.*}}
// CIR:         call void @llvm.memcpy.p0.p0.i64(ptr align 4 @guhfa
//
// OGCG-LABEL: define dso_local void @store_union_hfa()
// OGCG:        %[[OCALL:.*]] = call %union.UnionHFA @return_union_hfa()
// OGCG:        extractvalue %union.UnionHFA %[[OCALL]], 0
// OGCG:        call void @llvm.memcpy.p0.p0.i64(ptr align 4 @guhfa
void store_union_hfa(void) { guhfa = return_union_hfa(); }
