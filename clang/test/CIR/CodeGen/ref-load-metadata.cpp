// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Verify that loads from reference parameters carry CIR-level nonnull/align
// attributes and that direct CIR lowers them to final LLVM IR metadata.

// CIR-LABEL: cir.func{{.*}} @_Z11use_int_refRi
// CIR: cir.load {{.*}} {cir.nonnull, cir.ref_align = 4 : i64}
// LLVM-LABEL: define{{.*}} @_Z11use_int_refRi
// LLVM: load ptr, ptr %{{.*}}, align 8{{.*}}!nonnull ![[NONNULL:[0-9]+]]{{.*}}!align ![[ALIGN4:[0-9]+]]
void use_int_ref(int& r) { r = 42; }

struct S { int x; int y; };

// CIR-LABEL: cir.func{{.*}} @_Z14use_struct_refRK1S
// CIR: cir.load {{.*}} {cir.nonnull, cir.ref_align = 4 : i64}
// LLVM-LABEL: define{{.*}} @_Z14use_struct_refRK1S
// LLVM: load ptr, ptr %{{.*}}, align 8{{.*}}!nonnull ![[NONNULL]]{{.*}}!align ![[ALIGN4]]
int use_struct_ref(const S& s) { return s.x; }

// Char references have alignment 1, so !align should not be emitted (only > 1).
// CIR-LABEL: cir.func{{.*}} @_Z12use_char_refRc
// CIR: cir.load {{.*}} {cir.nonnull}
// LLVM-LABEL: define{{.*}} @_Z12use_char_refRc
// LLVM: load ptr, ptr %{{.*}}, align 8{{.*}}!nonnull ![[NONNULL]]
// LLVM-NOT: load ptr, ptr %{{.*}}, align 8{{.*}}!align
void use_char_ref(char& c) { c = 'a'; }
