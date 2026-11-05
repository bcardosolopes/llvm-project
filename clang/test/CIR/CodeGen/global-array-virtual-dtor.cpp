// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test: global array of class with virtual destructor. The global's
// sym type uses an anonymous struct (with vtable pointer fields) but the dtor
// expects the named record type. A bitcast is needed in the array dtor.

struct B { virtual ~B() {} };
B g_arr[1];

// CIR: cir.global {{.*}} @g_arr

// The array destructor helper bitcasts from the anonymous struct array type
// to the named record array type before iterating and calling the dtor.
// CIR: cir.func {{.*}} @__cxx_global_array_dtor
// CIR:   cir.get_global @g_arr
// CIR:   cir.cast bitcast
// CIR:   cir.cast array_to_ptrdecay

// LLVM: @g_arr = global [1 x
