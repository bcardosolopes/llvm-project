// RUN: %clang -fclangir -fsanitize=undefined -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NO-WARN --allow-empty
// RUN: %clang -fclangir -fsanitize=address -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NO-WARN --allow-empty
// RUN: %clang -fclangir -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NO-WARN --allow-empty
// RUN: %clang -fsanitize=undefined -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NO-WARN --allow-empty

// NO-WARN-NOT: sanitizer

int foo(int x) { return x + 1; }
