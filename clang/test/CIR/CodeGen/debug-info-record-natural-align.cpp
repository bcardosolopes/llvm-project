// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A record with only its natural alignment must NOT carry DW_AT_alignment on its
// DICompositeType: classic CodeGen emits the alignment only for explicit
// over-alignment (getDeclAlignIfRequired). When reconstructing record debug info
// during lowering, CIR stamped the record's natural alignment via
// astCtx.getTypeAlign, emitting a spurious align: 32 that classic omits (most
// visible on records reached through a function, e.g. as the scope of a member
// function). Recompute from the source decl (0 when no explicit alignment).

struct Plain {
  int x;
  int get() { return x; }
};

int useP(Plain *p) { return p->get(); }

// The naturally-aligned composite goes straight from size to flags with no align
// field in between (the identifier-bearing primary composite).
// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Plain"{{.*}}size: 32, flags: DIFlagTypePassByValue, elements: !{{[0-9]+}}, identifier:
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Plain"{{.*}}size: 32, flags: DIFlagTypePassByValue, elements: !{{[0-9]+}}, identifier:
