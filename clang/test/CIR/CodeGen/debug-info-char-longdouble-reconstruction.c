// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// When the debug-info base type is reconstructed from a CIR type (array element,
// pointer pointee, parameter, or local) rather than from a global/struct-field
// with surviving source-type info, two things were wrong:
//   - a plain `char` was named "signed char"
//   - `long double` reported byte_size 10 (its 80-bit significand) instead of
//     the 16-byte storage size, mis-striding arrays for debuggers.
// Both must match classic CodeGen.

struct S {
  char buf[4];
};

int use_char(struct S *s, char c) { return s->buf[0] + c; }
// CIR:  !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
// OGCG: !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
// CIR-NOT:  !DIBasicType(name: "signed char"
// OGCG-NOT: !DIBasicType(name: "signed char"

long double use_ld(int x) {
  long double ld = 1.5L;
  return (int)(ld + x);
}
// CIR:  !DIBasicType(name: "long double", size: 128, encoding: DW_ATE_float)
// OGCG: !DIBasicType(name: "long double", size: 128, encoding: DW_ATE_float)
