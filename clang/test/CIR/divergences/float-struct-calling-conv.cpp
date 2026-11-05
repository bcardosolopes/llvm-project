// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Floating-point struct calling conventions per the x86_64 SysV ABI:
// - Single float struct → float (XMM register)
// - Single double struct → double (XMM register)
// - Two floats struct → <2 x float> (XMM register)
// - Mixed int+float struct (one eightbyte) → i64 (INTEGER wins over SSE)
// - Three floats (12 bytes) → { <2 x float>, float } (two XMM registers)
// - Four floats (16 bytes) → { <2 x float>, <2 x float> } (two XMM registers)

struct FloatStruct { float f; };
FloatStruct return_float() { return {3.14f}; }
int test_float() { FloatStruct s = return_float(); return s.f > 3.0f ? 1 : 0; }

struct DoubleStruct { double d; };
DoubleStruct return_double() { return {3.14159}; }
int test_double() { DoubleStruct s = return_double(); return s.d > 3.0 ? 1 : 0; }

struct TwoFloats { float a, b; };
TwoFloats return_two_floats() { return {1.0f, 2.0f}; }
int test_two_floats() { TwoFloats s = return_two_floats(); return s.a + s.b > 2.5f ? 1 : 0; }

struct MixedStruct { int i; float f; };
MixedStruct return_mixed() { return {42, 3.14f}; }
int test_mixed() { MixedStruct s = return_mixed(); return s.i; }

struct ThreeFloats { float a, b, c; };
ThreeFloats return_three_floats() { return {1.0f, 2.0f, 3.0f}; }
int test_three_floats() { ThreeFloats s = return_three_floats(); return s.a + s.b + s.c > 5.0f ? 1 : 0; }

struct FourFloats { float a, b, c, d; };
FourFloats return_four_floats() { return {1.0f, 2.0f, 3.0f, 4.0f}; }
int test_four_floats() { FourFloats s = return_four_floats(); return s.a + s.b + s.c + s.d > 9.0f ? 1 : 0; }

// CIR: define dso_local float @_Z12return_floatv()
// CIR: define dso_local double @_Z13return_doublev()
// CIR: define dso_local <2 x float> @_Z17return_two_floatsv()
// CIR: define dso_local i64 @_Z12return_mixedv()
// CIR: define dso_local { <2 x float>, float } @_Z19return_three_floatsv()
// CIR: define dso_local { <2 x float>, <2 x float> } @_Z18return_four_floatsv()

// OGCG: define dso_local float @_Z12return_floatv()
// OGCG: define dso_local double @_Z13return_doublev()
// OGCG: define dso_local <2 x float> @_Z17return_two_floatsv()
// OGCG: define dso_local i64 @_Z12return_mixedv()
// OGCG: define dso_local { <2 x float>, float } @_Z19return_three_floatsv()
// OGCG: define dso_local { <2 x float>, <2 x float> } @_Z18return_four_floatsv()
