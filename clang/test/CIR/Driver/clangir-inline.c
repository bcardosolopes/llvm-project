// -fclangir-inline reaches cc1 only when asked for; the last of
// -f[no-]clangir-inline wins, and it is off by default at any -O level.
// -fclangir-inline-threshold= is passed through as given.
//
// RUN: %clang -fclangir -O2 -c %s -### 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang -fclangir -O2 -fclangir-inline -c %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ON
// RUN: %clang -fclangir -O2 -fclangir-inline -fno-clangir-inline -c %s -### \
// RUN:     2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang -fclangir -O2 -fno-clangir-inline -fclangir-inline -c %s -### \
// RUN:     2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang -fclangir -O2 -fclangir-inline -fclangir-inline-threshold=8 \
// RUN:     -c %s -### 2>&1 | FileCheck %s --check-prefix=THRESHOLD

// ON: "-cc1"
// ON-SAME: "-fclangir-inline"
// OFF: "-cc1"
// OFF-NOT: "-fclangir-inline"
// THRESHOLD: "-cc1"
// THRESHOLD-SAME: "-fclangir-inline"
// THRESHOLD-SAME: "-fclangir-inline-threshold=8"
