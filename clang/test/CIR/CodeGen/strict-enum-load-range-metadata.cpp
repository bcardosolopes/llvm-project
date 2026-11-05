// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -disable-llvm-passes \
// RUN:   -fstrict-enums -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -disable-llvm-passes \
// RUN:   -fstrict-enums -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

enum E { A = 0, B = 1, C = 2 };
E ge;

int f() { return ge; }

// CIR-LABEL: define dso_local noundef i32 @_Z1fv()
// CIR:      load i32, ptr @ge, align 4, !tbaa [[TBAA:![0-9]+]], !range [[RANGE:![0-9]+]], !noundef [[NOUNDEF:![0-9]+]]
// CIR-DAG:  [[RANGE]] = !{i32 0, i32 4}
// CIR-DAG:  [[NOUNDEF]] = !{}

// OGCG-LABEL: define dso_local noundef i32 @_Z1fv()
// OGCG:      load i32, ptr @ge, align 4, !tbaa [[TBAA_OG:![0-9]+]], !range [[RANGE_OG:![0-9]+]], !noundef [[NOUNDEF_OG:![0-9]+]]
// OGCG-DAG:  [[RANGE_OG]] = !{i32 0, i32 4}
// OGCG-DAG:  [[NOUNDEF_OG]] = !{}
