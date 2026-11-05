// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A plain `char` (BuiltinType::Char_S/Char_U) reached through a bitfield member
// or a reference was emitted as "signed char" because the debug-info paths that
// have the source BuiltinType lumped Char_S with SChar (and Char_U with UChar).
// Plain `char` must be named "char", matching classic CodeGen; only an explicit
// `signed char`/`unsigned char` gets that spelling.

struct Bits {
  char d : 2;
};
int use_bitfield(Bits *b) { return b->d; }

int use_ref(char &c) { return c; }

// CIR:  !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
// OGCG: !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
// CIR-NOT:  !DIBasicType(name: "signed char"
// OGCG-NOT: !DIBasicType(name: "signed char"
