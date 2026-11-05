// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// When two declarations produce the same mangled name but different
// types (e.g., extern "C" int + namespace float), CIR should handle the
// type conflict by replacing the old global with the new type, matching OGCG.

extern "C" { int _ZN2nm3abcE; }
namespace nm { float abc = 2; }

// LLVM: @_ZN2nm3abcE = global float 2.000000e+00, align 4
// OGCG: @_ZN2nm3abcE = global float 2.000000e+00, align 4
