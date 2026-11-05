// Verify that CIR emits canonical (null, not empty) DISubprogram linkageName
// metadata. The bug manifested as a bitcode reader assertion
// "Expected canonical MDString" when ld.lld loaded fat-LTO objects compiled
// with -fclangir -g because replaceRawLinkageName was fed an empty MDString
// instead of nullptr.
//
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir \
// RUN:   -debug-info-kind=limited -emit-llvm-bc %s -o %t.bc
// RUN: llvm-dis %t.bc -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

void foo(void) {}
int bar(int x) { return x + 1; }

// CHECK: !DISubprogram(name: "foo"
// CHECK-NOT: linkageName:
// CHECK-SAME: spFlags: DISPFlagDefinition
// CHECK: !DISubprogram(name: "bar"
// CHECK-NOT: linkageName:
// CHECK-SAME: spFlags: DISPFlagDefinition
