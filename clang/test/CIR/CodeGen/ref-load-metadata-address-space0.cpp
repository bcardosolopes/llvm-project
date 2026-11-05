// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Explicit AS0 type spelling should still produce !nonnull on reference loads.
// OGCG uses target address-space==0, not only LangAS::Default identity.

typedef int __attribute__((address_space(0))) as0_int;

// CIR-LABEL: cir.func{{.*}} @_Z1fR
// CIR: cir.load{{.*}} {cir.nonnull, cir.ref_align = 4 : i64}
int f(as0_int &r) { return r; }

// LLVM-LABEL: define{{.*}} @_Z1fR
// LLVM: load ptr, ptr %{{.*}}, align 8{{.*}}!nonnull !{{[0-9]+}}{{.*}}!align !{{[0-9]+}}

// OGCG-LABEL: define{{.*}} @_Z1fR
// OGCG: load ptr, ptr %{{.*}}, align 8, !nonnull !{{[0-9]+}}, !align !{{[0-9]+}}
