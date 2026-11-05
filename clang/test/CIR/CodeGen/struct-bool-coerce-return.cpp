// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Regression test: Bool field in struct return was coerced to i64
// instead of i8. CIR's BoolType is distinct from IntType, so
// GetINTEGERTypeAtOffset must handle it explicitly.

struct PtrBool { void *p; bool b; };
PtrBool test_ptr_bool() { return {}; }
// LLVM: define dso_local { ptr, i8 } @_Z13test_ptr_boolv()
// OGCG: define dso_local { ptr, i8 } @_Z13test_ptr_boolv()

struct BoolPtr { bool b; void *p; };
BoolPtr test_bool_ptr() { return {}; }
// LLVM: define dso_local { i8, ptr } @_Z13test_bool_ptrv()
// OGCG: define dso_local { i8, ptr } @_Z13test_bool_ptrv()

struct DoubleBool { double d; bool b; };
DoubleBool test_double_bool() { return {}; }
// LLVM: define dso_local { double, i8 } @_Z16test_double_boolv()
// OGCG: define dso_local { double, i8 } @_Z16test_double_boolv()

// Two bools in the second eightbyte — NOT tail padding, so stays i64.
struct PtrTwoBools { void *p; bool b1; bool b2; };
PtrTwoBools test_ptr_two_bools() { return {}; }
// LLVM: define dso_local { ptr, i64 } @_Z18test_ptr_two_boolsv()
// OGCG: define dso_local { ptr, i64 } @_Z18test_ptr_two_boolsv()
