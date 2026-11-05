// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A record type with an explicit over-alignment must record DW_AT_alignment on
// its DICompositeType (used here as a global and as a by-value parameter, both
// of which reconstruct the record type).

struct alignas(64) Over { int x; };

Over gover;
int useO(Over o) { return o.x; }

// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Over"{{.*}}align: 512
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Over"{{.*}}align: 512
