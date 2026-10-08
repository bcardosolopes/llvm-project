// RUN: %clang --target=x86_64-unknown-linux-gnu -fclangir -c %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang --target=x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fclangir-mem2reg -c %s -### 2>&1 | FileCheck %s --check-prefix=POS
// RUN: %clang --target=x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fno-clangir-mem2reg -c %s -### 2>&1 | FileCheck %s --check-prefix=NEG

// DEFAULT-NOT: clangir-mem2reg
// DEFAULT-NOT: unused-command-line-argument
// DEFAULT: "-cc1"

// POS: "-fclangir-mem2reg"
// POS-NOT: unused-command-line-argument

// NEG: "-fno-clangir-mem2reg"
// NEG-NOT: unused-command-line-argument

int add(int a, int b) {
  return a + b;
}
