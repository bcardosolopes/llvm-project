// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -O2 -disable-llvm-passes \
// RUN:   -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -O2 -disable-llvm-passes \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -O2 -disable-llvm-passes \
// RUN:   -fsanitize=bool -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR-SAN
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -O2 -disable-llvm-passes \
// RUN:   -fsanitize=bool -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-SAN

_Bool gb;

_Bool load_b(void) { return __atomic_load_n(&gb, __ATOMIC_RELAXED); }

// CIR-LABEL: define dso_local zeroext i1 @load_b()
// CIR:      load atomic i8, ptr @gb monotonic, align 1, !range [[RANGE:![0-9]+]], !noundef [[NOUNDEF:![0-9]+]]
// CIR-DAG:  [[RANGE]] = !{i8 0, i8 2}
// CIR-DAG:  [[NOUNDEF]] = !{}

// OGCG-LABEL: define dso_local zeroext i1 @load_b()
// OGCG:      load atomic i8, ptr @gb monotonic, align 1, !range [[RANGE_OG:![0-9]+]], !noundef [[NOUNDEF_OG:![0-9]+]]
// OGCG-DAG:  [[RANGE_OG]] = !{i8 0, i8 2}
// OGCG-DAG:  [[NOUNDEF_OG]] = !{}

// CIR-SAN-LABEL: define dso_local zeroext i1 @load_b()
// CIR-SAN:      load atomic i8, ptr @gb monotonic, align 1

// OGCG-SAN-LABEL: define dso_local zeroext i1 @load_b()
// OGCG-SAN:      load atomic i8, ptr @gb monotonic, align 1
// OGCG-SAN-NOT:  !range
// OGCG-SAN-NOT:  !noundef
