// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Verify that x86_64 ABI classification correctly handles base class
// subobjects. CIR stores base class data as the first N members of the
// RecordType; they must be classified separately from fields, matching
// OGCG's base class iteration (X86.cpp:2070-2098).

// --- Case 1: Simple single inheritance ---
// Base has an int (4 bytes), Derived adds a short (2 bytes).
// Total = 8 bytes, fits in Lo eightbyte as Integer.
struct Base1 { int x; };
struct Derived1 : Base1 { short y; };

// CIR: define dso_local void @_Z2f18Derived1(i64 %{{.*}})
// OGCG: define dso_local void @_Z2f18Derived1(i64 %{{.*}})
void f1(Derived1 d) {}

// --- Case 2: Multiple inheritance ---
// BaseA has an int (4 bytes), BaseB has a float (4 bytes).
// Derived2 adds nothing. Total = 8 bytes.
// Both in same eightbyte: merge(Integer, SSE) = Integer.
struct BaseA { int a; };
struct BaseB { float b; };
struct Derived2 : BaseA, BaseB {};

// CIR: define dso_local void @_Z2f28Derived2(i64 %{{.*}})
// OGCG: define dso_local void @_Z2f28Derived2(i64 %{{.*}})
void f2(Derived2 d) {}

// --- Case 3: Base with SSE field, derived adds Integer ---
// Base has a double (8 bytes, SSE), Derived adds an int (4 bytes, Integer).
// Total = 16 bytes: Lo = SSE (double in first eightbyte),
// Hi = Integer (int in second eightbyte).
struct Base3 { double d; };
struct Derived3 : Base3 { int z; };

// CIR: define dso_local void @_Z2f38Derived3(double %{{.*}}, i32 %{{.*}})
// OGCG: define dso_local void @_Z2f38Derived3(double %{{.*}}, i32 %{{.*}})
void f3(Derived3 d) {}

// --- Case 4: Empty base class ---
// Empty base should contribute NoClass, not Integer.
struct EmptyBase {};
struct Derived4 : EmptyBase { long val; };

// CIR: define dso_local void @_Z2f48Derived4(i64 %{{.*}})
// OGCG: define dso_local void @_Z2f48Derived4(i64 %{{.*}})
void f4(Derived4 d) {}
