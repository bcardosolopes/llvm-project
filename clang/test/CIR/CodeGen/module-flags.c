// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -pic-level 2 -pic-is-pie -fclangir -emit-cir %s -o %t-pic.cir
// RUN: FileCheck --check-prefix=CIR-PIC --input-file=%t-pic.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -pic-level 2 -pic-is-pie -fclangir -emit-llvm %s -o %t-pic.ll
// RUN: FileCheck --check-prefix=LLVM-PIC --input-file=%t-pic.ll %s

int x = 42;

// CIR: module {{.*}} attributes {
// CIR-SAME: cir.ident = "{{[^"]+}}"
// CIR-SAME: cir.wchar_size = 4 : i64

// CIR-PIC: module {{.*}} attributes {
// CIR-PIC-SAME: cir.pic_level = 2 : i64
// CIR-PIC-SAME: cir.pie_level = 2 : i64
// CIR-PIC-SAME: cir.wchar_size = 4 : i64

// LLVM: !llvm.ident = !{![[#IDENT:]]}
// LLVM: !llvm.module.flags = !{![[#]]
// LLVM: ![[#IDENT]] = !{!"{{[^"]+}}"}
// LLVM: ![[#]] = !{i32 1, !"wchar_size", i32 4}

// LLVM-PIC: !llvm.module.flags = !{![[#]]
// LLVM-PIC: ![[#]] = !{i32 8, !"PIC Level", i32 2}
// LLVM-PIC: ![[#]] = !{i32 7, !"PIE Level", i32 2}
// LLVM-PIC: ![[#]] = !{i32 1, !"wchar_size", i32 4}
