// A function that records its own language is lowered by that language's ABI
// rules, not the module's.
//
// A module from one TU has one language, but cir-link merges C and C++ TUs
// into one module, and CallConvLowering took every function's ABI from the
// module's cir.lang. The language is part of the ABI: C++ marks a returned
// value noundef ("strict return"), and C must not, because a C function may
// fall off its end when the caller ignores the result. A C function lowered as
// C++ got a noundef return that LLVM can treat as undefined behaviour.
//
// The module below is C++; one function carries cir.lang = c, the mark cir-link
// puts on functions from C inputs.
//
// RUN: %clang_cc1 -triple aarch64-unknown-linux-android -fclangir -emit-cir \
// RUN:     -fno-clangir-call-conv-lowering %s -o %t.cir
// RUN: sed -E '/@_Z6from_ci\(/ s/ extra\(#fn_attr\) \{$/ extra(#fn_attr) attributes {cir.lang = #cir.lang<c>} {/' \
// RUN:     %t.cir > %t.stamped.cir
//
// The precondition: the stamp landed on exactly that function.
// RUN: FileCheck --input-file=%t.stamped.cir %s --check-prefix=STAMP
// STAMP: cir.func {{.*}}@_Z6from_ci({{.*}} attributes {cir.lang = #cir.lang<c>}
//
// RUN: cir-opt %t.stamped.cir --cir-call-conv-lowering -o - | FileCheck %s
//
// The C++ function keeps its strict return; the C one does not have one, and
// the call to it agrees.
// CHECK-DAG: cir.func {{.*}}@_Z8from_cxxi({{.*}}) -> (!s32i {cir.noundef})
// CHECK-DAG: cir.func {{.*}}@_Z6from_ci({{.*}}) -> !s32i extra
//
// Unstamped, both are C++, as before.
// RUN: cir-opt %t.cir --cir-call-conv-lowering -o - | FileCheck %s --check-prefix=CXX
// CXX: cir.func {{.*}}@_Z6from_ci({{.*}}) -> (!s32i {cir.noundef})

int from_cxx(int x) { return x; }
int from_c(int x) { return x; }
int caller(int x) { return from_cxx(x) + from_c(x); }
