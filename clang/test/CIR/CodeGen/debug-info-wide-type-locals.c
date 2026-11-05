// RUN: %clang -target x86_64-unknown-linux-gnu -O0 -g -S -emit-llvm -fclangir %s \
// RUN:   -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang -target x86_64-unknown-linux-gnu -O0 -g -S -emit-llvm %s \
// RUN:   -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// At -O0 with debug info, local variables of "wide" types — x86 80-bit long
// double, __int128, and _Complex — must retain a memory location. CIR used to
// promote them to SSA and express them with dbg_value, which the -O0 backend
// cannot turn into a DWARF location for these types, dropping DW_AT_location and
// making the variables invisible to debuggers. The driver now leaves mem2reg off
// at -O0, so every local gets a dbg_declare just like classic CodeGen.

int wide_locals(int x) {
  long double ld = 1.5L;
  __int128 i128 = 1;
  _Complex double c = 1.0 + 2.0i;
  ld += x;
  i128 += x;
  return (int)ld + (int)i128 + (int)__real__ c;
}

// CIR-LABEL: define {{.*}}@wide_locals(
// CIR: #dbg_declare(
// CIR-NOT: #dbg_value(
// CIR-DAG: !DILocalVariable(name: "ld"
// CIR-DAG: !DILocalVariable(name: "i128"
// CIR-DAG: !DILocalVariable(name: "c"

// OGCG-LABEL: define {{.*}}@wide_locals(
// OGCG: #dbg_declare(
// OGCG-NOT: #dbg_value(
// OGCG-DAG: !DILocalVariable(name: "ld"
// OGCG-DAG: !DILocalVariable(name: "i128"
// OGCG-DAG: !DILocalVariable(name: "c"
