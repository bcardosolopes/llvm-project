// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -disable-llvm-passes \
// RUN:   -fstrict-enums -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -disable-llvm-passes \
// RUN:   -fstrict-enums -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -disable-llvm-passes \
// RUN:   -fstrict-enums -fsanitize=enum -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR-SAN
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -disable-llvm-passes \
// RUN:   -fstrict-enums -fsanitize=enum -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-SAN

enum E { A = 0, B = 1, C = 2 };
extern E ge;

E load_e() { return __atomic_load_n(&ge, __ATOMIC_RELAXED); }

// CIR-LABEL: define dso_local noundef i32 @_Z6load_ev()
// CIR:      load atomic i32, ptr @ge monotonic, align 4, !range [[RANGE:![0-9]+]], !noundef [[NOUNDEF:![0-9]+]]
// CIR-DAG:  [[RANGE]] = !{i32 0, i32 4}
// CIR-DAG:  [[NOUNDEF]] = !{}

// OGCG-LABEL: define dso_local noundef i32 @_Z6load_ev()
// OGCG:      load atomic i32, ptr @ge monotonic, align 4, !range [[RANGE_OG:![0-9]+]], !noundef [[NOUNDEF_OG:![0-9]+]]
// OGCG-DAG:  [[RANGE_OG]] = !{i32 0, i32 4}
// OGCG-DAG:  [[NOUNDEF_OG]] = !{}

// CIR-SAN-LABEL: define dso_local noundef i32 @_Z6load_ev()
// CIR-SAN:      load atomic i32, ptr @ge monotonic, align 4

// OGCG-SAN-LABEL: define dso_local noundef i32 @_Z6load_ev()
// OGCG-SAN:      load atomic i32, ptr @ge monotonic, align 4
// OGCG-SAN-NOT:  !range
// OGCG-SAN-NOT:  !noundef
