// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -std=c++20 %s -o /dev/null 2>&1 | FileCheck --check-prefix=WARN %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -std=c++20 %s -o /dev/null 2>&1 | FileCheck --check-prefix=WARN %s

// Test that [[likely]]/[[unlikely]] on the body of an infinite while loop
// produces a warning diagnostic, matching OGCG behavior.

void test_likely_infinite() {
  while (true) [[likely]] { // expected-warning {{attribute 'likely' has no effect when annotating an infinite loop}}
    break;
  }
}

// WARN: warning: attribute 'likely' has no effect when annotating an infinite loop
// WARN: note: annotating the infinite loop here

void test_unlikely_infinite() {
  while (1) [[unlikely]] {
    break;
  }
}

// Non-infinite loop should NOT produce the diagnostic.
void test_likely_normal(bool cond) {
  while (cond) [[likely]] {
    break;
  }
}
