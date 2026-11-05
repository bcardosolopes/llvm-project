// RUN: %clang -target x86_64-unknown-linux-gnu -fclangir -fclangir-fat-objects \
// RUN:   -O2 -c %s -o %t.o
// RUN: llvm-readelf -S %t.o | FileCheck %s --check-prefix=SECTION
// RUN: llvm-objcopy --dump-section=.cir=%t.cir %t.o /dev/null
// RUN: cir-opt %t.cir | FileCheck %s --check-prefix=CIR

// Fat objects are the on-ramp for a CIR link-time pipeline: the object holds
// native code *and* the CIR it was lowered from, so a link can consume either
// one without a second compile of the source.
//
// Three things have to hold, and each is a separate way for this to be
// useless:
//
//   1. the section exists and is SHF_EXCLUDE, so the CIR rides along in the
//      .o but the linker drops it and the shipped artifact stays clean;
//   2. what is in it is valid CIR bytecode that parses back in a fresh
//      MLIRContext -- the AST-backed attributes have no serialized form and
//      must have been stripped, or nothing round-trips at all;
//   3. the CIR is the pre-lowering module, not something already flattened
//      to LLVM-dialect shapes.

// SECTION: .cir
// SECTION-SAME: E{{ }}

struct Point {
  int x, y;
  int sum() const { return x + y; }
};

int compute(Point p) { return p.sum(); }

// Parsed back from bytecode in cir-opt's own context: still CIR, still
// carrying the record type, and no AST attribute survived to trip the reader.
// CIR: !rec_Point = !cir.record<struct "Point"
// CIR: cir.func{{.*}}@_Z7compute5Point
// CIR-NOT: #cir.function.decl.ast
