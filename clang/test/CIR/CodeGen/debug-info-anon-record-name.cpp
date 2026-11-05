// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A lambda closure type is anonymous; its DICompositeType must not carry the
// CIR-internal "anon.N" name (classic leaves it unnamed).

int f(int a) {
  int captured = a;
  auto lam = [captured](int x) { return captured + x; };
  return lam(5);
}

// The lambda closure type is emitted unnamed: "scope:" follows the tag directly,
// with no "name:" operand (which would hold the leaked "anon.N").
// CIR: !DICompositeType(tag: DW_TAG_class_type, scope:
// OGCG: !DICompositeType(tag: DW_TAG_class_type, scope:
