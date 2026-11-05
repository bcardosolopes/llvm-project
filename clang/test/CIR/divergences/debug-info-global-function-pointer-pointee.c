// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: direct-CIR used to over-specify the plain-C
// DISubroutineType calling convention and dropped the return statement column.
// The remaining full-file diff noise here is non-semantic DIFile formatting.

int foo(void) { return 0; }
int (*fp)(void) = &foo;

// CHECK: ![[SUBROUTINE:[0-9]+]] = !DISubroutineType(types: ![[TYPES:[0-9]+]])
// CHECK-NOT: !DISubroutineType(cc: DW_CC_normal
// CHECK: ![[FOO:[0-9]+]] = distinct !DISubprogram(name: "foo"
// CHECK: !DILocation(line: {{[0-9]+}}, column: 17, scope: ![[FOO]])
