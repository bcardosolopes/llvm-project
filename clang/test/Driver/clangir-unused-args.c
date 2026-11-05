// RUN: %clang -fsyntax-only -Werror=unused-command-line-argument -fno-clangir %s -### 2>&1 | FileCheck %s --check-prefix=NO-UNUSED
// RUN: %clang -fsyntax-only -Werror=unused-command-line-argument -fclangir %s -### 2>&1 | FileCheck %s --check-prefix=NO-UNUSED
// RUN: %clang -c %s -### 2>&1 | FileCheck %s --check-prefix=NO-MEM2REG
// RUN: %clang -fclangir -c %s -### 2>&1 | FileCheck %s --check-prefix=NO-MEM2REG
// RUN: %clang -fclangir -O0 -c %s -### 2>&1 | FileCheck %s --check-prefix=NO-MEM2REG
// RUN: %clang -fclangir -O2 -c %s -### 2>&1 | FileCheck %s --check-prefix=WITH-MEM2REG

// NO-UNUSED-NOT: argument unused during compilation
// NO-MEM2REG-NOT: "-fclangir-mem2reg"
// WITH-MEM2REG: "-fclangir-mem2reg"
