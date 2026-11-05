// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Verify that a forward-declared enum with a non-32-bit underlying type
// gets the correct CIR type after completion, not the speculated u32.

enum E : unsigned long long;

// Force the forward-declared enum type into the type cache.
void use_fwd(E e);
void call_fwd(E e) { use_fwd(e); }

// Now complete the enum with its actual u64 underlying type.
enum E : unsigned long long { A = 0, B = 1ull << 40 };

E get() { return B; }
// CHECK: cir.func{{.*}} @_Z3getv
// CHECK:   cir.const #cir.int<1099511627776> : !u64i
