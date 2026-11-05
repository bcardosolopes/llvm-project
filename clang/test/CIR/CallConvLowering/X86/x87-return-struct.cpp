// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Verify that a struct containing only long double (x86_fp80) is returned
// via the X87 stack as a raw x86_fp80 value per AMD64 ABI Rule 6.

struct LDStruct { long double ld; };

LDStruct get_ld(long double v) { return {v}; }

void use() { auto s = get_ld(3.14L); }

// CIR: cir.func{{.*}} @_Z6get_lde(%arg0: !cir.long_double<!cir.f80>
// CIR:   cir.return {{%.*}} : !cir.long_double<!cir.f80>

// LLVM: define{{.*}} x86_fp80 @_Z6get_lde(x86_fp80 noundef %{{.*}})
// LLVM:   ret x86_fp80 %{{.*}}

// OGCG: define{{.*}} x86_fp80 @_Z6get_lde(x86_fp80 {{.*}}%{{.*}})
// OGCG:   ret x86_fp80 %{{.*}}
