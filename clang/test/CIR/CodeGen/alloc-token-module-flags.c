// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=alloc-token -emit-llvm -o - %s | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=alloc-token -falloc-token-mode=increment -emit-llvm -o - %s | FileCheck %s --check-prefix=INCREMENT
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=alloc-token -falloc-token-max=100 -emit-llvm -o - %s | FileCheck %s --check-prefix=MAX
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=alloc-token -fsanitize-alloc-token-fast-abi -emit-llvm -o - %s | FileCheck %s --check-prefix=FASTABI
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=alloc-token -fsanitize-alloc-token-extended -emit-llvm -o - %s | FileCheck %s --check-prefix=EXTENDED

// DEFAULT-NOT: !"alloc-token-mode"
// DEFAULT-NOT: !"alloc-token-max"
// DEFAULT-NOT: !"alloc-token-fast-abi"
// DEFAULT-NOT: !"alloc-token-extended"

// INCREMENT: !llvm.module.flags = !{{{.*}}}
// INCREMENT: !{{[0-9]+}} = !{i32 1, !"alloc-token-mode", !"increment"}

// MAX: !llvm.module.flags = !{{{.*}}}
// MAX: !{{[0-9]+}} = !{i32 1, !"alloc-token-max", i64 100}

// FASTABI: !llvm.module.flags = !{{{.*}}}
// FASTABI: !{{[0-9]+}} = !{i32 1, !"alloc-token-fast-abi", i32 1}

// EXTENDED: !llvm.module.flags = !{{{.*}}}
// EXTENDED: !{{[0-9]+}} = !{i32 1, !"alloc-token-extended", i32 1}
