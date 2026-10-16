// RUN: %clang --target=x86_64-unknown-linux-gnu -fclangir -c %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang --target=x86_64-unknown-linux-gnu -fclangir \
// RUN:   -clangir-disable-verifier -c %s -### 2>&1 | FileCheck %s --check-prefix=DISABLE

// DEFAULT-NOT: clangir-disable-verifier
// DEFAULT-NOT: unused-command-line-argument
// DEFAULT: "-cc1"

// DISABLE: "-clangir-disable-verifier"
// DISABLE-NOT: unused-command-line-argument

int add(int a, int b) {
  return a + b;
}
