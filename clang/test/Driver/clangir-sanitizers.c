// RUN: %clang -fclangir -fsanitize=undefined -c %s -### 2>&1 | FileCheck %s --check-prefix=UBSAN
// REQUIRES: cir-support
// RUN: %clang -fclangir -fsanitize=address -c %s -### 2>&1 | FileCheck %s --check-prefix=ASAN

// UBSAN: "-fsanitize={{[^"]*}}signed-integer-overflow{{[^"]*}}"
// ASAN: "-fsanitize=address"
