// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -main-file-name %s -fclangir \
// RUN:   -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=STDIN

// With -main-file-name (a normal driver build), the compile unit's DW_AT_name is
// the real source file -- not a synthetic "<stdin>" -- so debuggers and
// symbolizers can locate the source, matching classic CodeGen. Without
// -main-file-name (a bare -cc1 invocation) classic falls back to a "<stdin>"
// basename rooted at the source directory, and CIR now mirrors that as well.

int add(int a, int b) { return a + b; }

// CHECK: !DICompileUnit({{.*}}file: ![[CUFILE:[0-9]+]]
// CHECK: ![[CUFILE]] = !DIFile(filename: "{{.*}}debug-info-compile-unit-filename.c", directory: "")
// CHECK-NOT: <stdin>

// STDIN: !DICompileUnit({{.*}}file: ![[SCUFILE:[0-9]+]]
// STDIN: ![[SCUFILE]] = !DIFile(filename: "{{.*}}<stdin>", directory: "")
