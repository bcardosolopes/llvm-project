// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -fexceptions -emit-llvm %s -o %t-cir-eh.ll
// RUN: FileCheck --input-file=%t-cir-eh.ll %s -check-prefix=LLVM-EH

int add(int a, int b) { return a + b; }

// CIR: cir.func{{.*}} @_Z3addii({{.*}}) -> ({{.*}}) attributes {{{.*}}nothrow} {

// LLVM: define{{.*}} @_Z3addii(i32 noundef {{.*}}, i32 noundef {{.*}}) #[[ATTR:[0-9]+]]
// LLVM: attributes #[[ATTR]] = { {{.*}}nounwind{{.*}} }

// LLVM-EH: define{{.*}} @_Z3addii(
// LLVM-EH-NOT: nounwind
