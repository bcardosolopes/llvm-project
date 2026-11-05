// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c++20 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that C++ main gets norecurse attribute per [basic.start.main]p3.
// Matches OGCG CodeGenFunction.cpp:1080-1084.

// CIR-DAG: #[[MAIN_ATTR:fn_attr[0-9]*]] = #cir<extra({{{.*}}norecurse{{.*}}})>
// CIR-DAG: cir.func {{.*}} @main() {{.*}} extra(#[[MAIN_ATTR]])

// LLVM: define {{.*}} @main() #[[MAIN_ATTRS:[0-9]+]]
// LLVM: attributes #[[MAIN_ATTRS]] = {{{.*}}norecurse{{.*}}}

int main() { return 0; }
