// Verify that under the AAPCS-VFP hard-float ABI, ClangIR passes and returns
// homogeneous floating-point / vector aggregates (HFA / HVA) directly in their
// natural type (so the backend places them in VFP registers), matching classic
// CodeGen (OGCG). Non-homogeneous aggregates and over-large aggregates must stay
// on the soft-float AAPCS coercion path (core registers / sret).

// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +vfp3 -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +vfp3 -emit-llvm %s -o - | FileCheck --check-prefix=OGCG %s

struct HFA2 { float a, b; };
struct HFA3 { float a, b, c; };
struct HFA4 { float a, b, c, d; };
struct HVA2 { double x, y; };
struct HVA4 { double a, b, c, d; };
struct NonHFA { int i, j; };
struct Big5 { float a, b, c, d, e; };
typedef float f32x2 __attribute__((vector_size(8)));
struct HVec { f32x2 a, b; };

// An HFA of 2-4 floats is passed and returned directly in VFP registers.
// CHECK: define dso_local %struct.HFA2 @_Z9take_hfa24HFA2(%struct.HFA2 {{.*}})
// OGCG:  define dso_local %struct.HFA2 @_Z9take_hfa24HFA2(%struct.HFA2 {{.*}})
HFA2 take_hfa2(HFA2 v) { return v; }

// CHECK: define dso_local %struct.HFA3 @_Z9take_hfa34HFA3(%struct.HFA3 {{.*}})
// OGCG:  define dso_local %struct.HFA3 @_Z9take_hfa34HFA3(%struct.HFA3 {{.*}})
HFA3 take_hfa3(HFA3 v) { return v; }

// CHECK: define dso_local %struct.HFA4 @_Z9take_hfa44HFA4(%struct.HFA4 {{.*}})
// OGCG:  define dso_local %struct.HFA4 @_Z9take_hfa44HFA4(%struct.HFA4 {{.*}})
HFA4 take_hfa4(HFA4 v) { return v; }

// An HVA of 2-4 doubles is likewise passed and returned directly.
// CHECK: define dso_local %struct.HVA2 @_Z9take_hva24HVA2(%struct.HVA2 {{.*}})
// OGCG:  define dso_local %struct.HVA2 @_Z9take_hva24HVA2(%struct.HVA2 {{.*}})
HVA2 take_hva2(HVA2 v) { return v; }

// CHECK: define dso_local %struct.HVA4 @_Z9take_hva44HVA4(%struct.HVA4 {{.*}})
// OGCG:  define dso_local %struct.HVA4 @_Z9take_hva44HVA4(%struct.HVA4 {{.*}})
HVA4 take_hva4(HVA4 v) { return v; }

// A homogeneous aggregate of two 64-bit vectors is an HVA.
// CHECK: define dso_local %struct.HVec @_Z9take_hvec4HVec(%struct.HVec {{.*}})
// OGCG:  define dso_local %struct.HVec @_Z9take_hvec4HVec(%struct.HVec {{.*}})
HVec take_hvec(HVec v) { return v; }

// _Complex float is treated as a 2-element float HFA.
// CHECK: define dso_local {{.*}}{ float, float } @_Z9take_cplxCf({ float, float } {{.*}})
// OGCG:  define dso_local {{.*}}{ float, float } @_Z9take_cplxCf({ float, float } {{.*}})
_Complex float take_cplx(_Complex float v) { return v; }

// A struct of two ints is NOT homogeneous: it must use the AAPCS soft-float
// coercion (sret for the return, [2 x i32] for the argument), not VFP.
// CHECK: define dso_local void @_Z11take_nonhfa6NonHFA(ptr {{.*}}sret(%struct.NonHFA) {{.*}}, [2 x i32] {{.*}})
// OGCG:  define dso_local void @_Z11take_nonhfa6NonHFA(ptr {{.*}}sret(%struct.NonHFA) {{.*}}, [2 x i32] {{.*}})
NonHFA take_nonhfa(NonHFA v) { return v; }

// Five floats exceed the 4-member HFA bound and fall back to soft-float.
// CHECK: define dso_local void @_Z9take_big54Big5(ptr {{.*}}sret(%struct.Big5) {{.*}}, [5 x i32] {{.*}})
// OGCG:  define dso_local void @_Z9take_big54Big5(ptr {{.*}}sret(%struct.Big5) {{.*}}, [5 x i32] {{.*}})
Big5 take_big5(Big5 v) { return v; }

// A scalar float is unaffected.
// CHECK: define dso_local {{.*}}float @_Z11take_scalarf(float {{.*}})
// OGCG:  define dso_local {{.*}}float @_Z11take_scalarf(float {{.*}})
float take_scalar(float v) { return v; }
