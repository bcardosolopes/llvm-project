// Under the AAPCS-VFP hard-float ABI, __fp16 (and __bf16) are NOT valid
// homogeneous-aggregate base types (unlike AAPCS64), so a struct of __fp16
// members must NOT be passed/returned in VFP registers. It falls back to the
// soft-float AAPCS coercion path (integer coercion for small aggregates, sret
// for larger ones), matching classic CodeGen (OGCG). A struct of float, by
// contrast, IS a valid HFA and is still passed/returned directly.

// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +vfp3 -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -target-feature +vfp3 -emit-llvm %s -o - | FileCheck --check-prefix=OGCG %s

struct H1 { __fp16 a; };
struct H2 { __fp16 a, b; };
struct H3 { __fp16 a, b, c; };
struct H4 { __fp16 a, b, c, d; };

// A one-__fp16 struct is coerced to i16 (not returned as an HFA by value).
// CHECK: define dso_local i16 @mk1()
// OGCG:  define dso_local i16 @mk1()
struct H1 mk1(void) { struct H1 h = {0}; return h; }

// Two __fp16 (4 bytes) -> i32.
// CHECK: define dso_local i32 @mk2()
// OGCG:  define dso_local i32 @mk2()
struct H2 mk2(void) { struct H2 h = {0, 0}; return h; }

// Three __fp16 (6 bytes) -> sret.
// CHECK: define dso_local void @mk3(ptr {{.*}} sret(%struct.H3) {{.*}})
// OGCG:  define dso_local void @mk3(ptr {{.*}} sret(%struct.H3) {{.*}})
struct H3 mk3(void) { struct H3 h = {0, 0, 0}; return h; }

// Four __fp16 (8 bytes) -> sret return, [2 x i32] argument coercion.
// CHECK: define dso_local void @mkh(ptr {{.*}} sret(%struct.H4) {{.*}})
// OGCG:  define dso_local void @mkh(ptr {{.*}} sret(%struct.H4) {{.*}})
struct H4 mkh(void) { struct H4 h = {0, 0, 0, 0}; return h; }

// CHECK: define dso_local void @idh(ptr {{.*}} sret(%struct.H4) {{.*}}, [2 x i32] {{.*}})
// OGCG:  define dso_local void @idh(ptr {{.*}} sret(%struct.H4) {{.*}}, [2 x i32] {{.*}})
struct H4 idh(struct H4 x) { return x; }

// Regression: a struct of float IS a valid HFA base type and must still be
// passed and returned directly in VFP registers.
struct F4 { float a, b, c, d; };

// CHECK: define dso_local %struct.F4 @idf4(%struct.F4 {{.*}})
// OGCG:  define dso_local %struct.F4 @idf4(%struct.F4 {{.*}})
struct F4 idf4(struct F4 x) { return x; }
