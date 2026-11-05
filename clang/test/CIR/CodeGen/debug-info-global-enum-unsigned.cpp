// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Enumerators of an enum with an unsigned underlying type must be marked
// isUnsigned in debug info, and large/wrapping values must be preserved (not
// sign-extended). A signed-underlying enum keeps signed enumerators.

enum U : unsigned char { UA, UB = 255 };
enum V : unsigned int { VA, VB = 4000000000u };
enum class S : long { SA = -1, SB = 100 };

U gu = UB;
V gv = VB;
S gs = S::SB;

// CIR-DAG: !DIEnumerator(name: "UB", value: 255, isUnsigned: true)
// CIR-DAG: !DIEnumerator(name: "VB", value: 4000000000, isUnsigned: true)
// CIR-DAG: !DIEnumerator(name: "SA", value: -1)
// CIR-DAG: !DIEnumerator(name: "SB", value: 100)

// OGCG-DAG: !DIEnumerator(name: "UB", value: 255, isUnsigned: true)
// OGCG-DAG: !DIEnumerator(name: "VB", value: 4000000000, isUnsigned: true)
// OGCG-DAG: !DIEnumerator(name: "SA", value: -1)
// OGCG-DAG: !DIEnumerator(name: "SB", value: 100)
