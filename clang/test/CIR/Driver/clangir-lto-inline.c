// At a -fclangir-lto link, -fclangir-inline asks the wrapper to run the CIR
// inliner when it lowers the merged module, with -fclangir-inline-threshold=
// passed on.
//
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto \
// RUN:     -fclangir-inline -shared %s -o %t.so 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ON
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto \
// RUN:     -shared %s -o %t.so 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto \
// RUN:     -fclangir-inline -fno-clangir-inline -shared %s -o %t.so 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OFF
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto \
// RUN:     -fclangir-inline -fclangir-inline-threshold=8 -shared %s -o %t.so \
// RUN:     2>&1 | FileCheck %s --check-prefix=THRESHOLD

// ON: cir-link-wrapper
// ON-SAME: "--cir-inline"
// OFF: cir-link-wrapper
// OFF-NOT: "--cir-inline"
// OFF-NOT: "--cir-inline-threshold
// THRESHOLD: cir-link-wrapper
// THRESHOLD-SAME: "--cir-inline" "--cir-inline-threshold=8"
