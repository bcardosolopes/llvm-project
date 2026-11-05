// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes \
// RUN:   -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes \
// RUN:   -fsanitize=bool -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR-SAN
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes \
// RUN:   -fsanitize=bool -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-SAN

_Bool g;

int f(void) { return g; }

// CIR-LABEL: define dso_local i32 @f()
// CIR:      load i8, ptr @g, align 1, !tbaa [[TBAA:![0-9]+]], !range [[RANGE:![0-9]+]], !noundef [[NOUNDEF:![0-9]+]]
// CIR-DAG:  [[RANGE]] = !{i8 0, i8 2}
// CIR-DAG:  [[NOUNDEF]] = !{}

// OGCG-LABEL: define dso_local i32 @f()
// OGCG:      load i8, ptr @g, align 1, !tbaa [[TBAA_OG:![0-9]+]], !range [[RANGE_OG:![0-9]+]], !noundef [[NOUNDEF_OG:![0-9]+]]
// OGCG-DAG:  [[RANGE_OG]] = !{i8 0, i8 2}
// OGCG-DAG:  [[NOUNDEF_OG]] = !{}

// CIR-SAN-LABEL: define dso_local i32 @f()
// CIR-SAN:      load i8, ptr @g, align 1, !tbaa [[SAN_TBAA:![0-9]+]]

// OGCG-SAN-LABEL: define dso_local i32 @f()
// OGCG-SAN:      load i8, ptr @g, align 1, !tbaa [[SAN_TBAA_OG:![0-9]+]]
// OGCG-SAN-NOT:  !range
// OGCG-SAN-NOT:  !noundef
