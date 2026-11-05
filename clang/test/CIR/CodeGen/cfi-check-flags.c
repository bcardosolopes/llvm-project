// RUN: %clang_cc1 -triple x86_64-unknown-linux -O0 -fclangir \
// RUN:     -fsanitize-cfi-cross-dso \
// RUN:     -fsanitize=cfi-icall,cfi-nvcall,cfi-vcall,cfi-unrelated-cast,cfi-derived-cast \
// RUN:     -emit-llvm -o - %s | FileCheck %s --check-prefix=CROSS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -emit-llvm -fsanitize=kcfi \
// RUN:     -fsanitize-cfi-icall-experimental-normalize-integers \
// RUN:     -o - %s | FileCheck %s --check-prefix=NORM

void caller(void (*f)(void)) {
  f();
}

void foo(void (*fn)(int), int arg) {
  fn(arg);
}

// CROSS: define weak void @__cfi_check(i64 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}){{.*}} align 4096
// CROSS: !{i32 4, !"Cross-DSO CFI", i32 1}

// NORM: !{i32 4, !"cfi-normalize-integers", i32 1}
