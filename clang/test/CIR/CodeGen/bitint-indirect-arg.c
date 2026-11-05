// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Test that _BitInt(>128) types are passed indirectly via byval/sret,
// matching OGCG behavior. Call conv lowering must handle indirect byval
// scalar args without crashing.

typedef _BitInt(256) i256;

// CIR-LABEL: @pass256
// CIR: cir.sret = !s256i
// CIR-SAME: cir.byval = !s256i

// LLVM: define {{.*}} @pass256(ptr {{.*}} sret(i256) {{.*}}, ptr {{.*}} byval(i256) {{.*}})
// LLVM:   load i256
// LLVM:   store i256
// LLVM:   ret void

// OGCG: define {{.*}} @pass256(ptr {{.*}} sret(i256) {{.*}}, ptr {{.*}} byval(i256) {{.*}})
i256 pass256(i256 x) { return x; }

// CIR-LABEL: @add256
// CIR: cir.sret = !s256i
// CIR-SAME: cir.byval = !s256i
// CIR-SAME: cir.byval = !s256i

// LLVM: define {{.*}} @add256(ptr {{.*}} sret(i256) {{.*}}, ptr {{.*}} byval(i256) {{.*}}, ptr {{.*}} byval(i256) {{.*}})
// LLVM:   %[[A:.*]] = load i256
// LLVM:   %[[B:.*]] = load i256
// LLVM:   add nsw i256 %[[A]], %[[B]]
// LLVM:   ret void

// OGCG: define {{.*}} @add256(ptr {{.*}} sret(i256) {{.*}}, ptr {{.*}} byval(i256) {{.*}}, ptr {{.*}} byval(i256) {{.*}})
i256 add256(i256 a, i256 b) { return a + b; }
