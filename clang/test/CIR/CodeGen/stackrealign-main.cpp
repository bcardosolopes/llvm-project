// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -mstack-alignment=32 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -mstack-alignment=32 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -mstack-alignment=32 %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// When -mstack-alignment=N is specified, main() needs stackrealign because
// the C runtime calls main with default alignment, which may differ.
// Matches OGCG CodeGenFunction.cpp:1098-1102.

// CIR-DAG: #[[MAIN_ATTR:fn_attr[0-9]*]] = #cir<extra({{{.*}}stackrealign{{.*}}})>
// CIR-DAG: cir.func {{.*}} @main() {{.*}} extra(#[[MAIN_ATTR]])

// LLVM: define {{.*}} @main() #[[MAIN_ATTRS:[0-9]+]]
// LLVM: attributes #[[MAIN_ATTRS]] = {{{.*}}stackrealign{{.*}}}

// OGCG: define {{.*}} @main() #[[MAIN_ATTRS:[0-9]+]]
// OGCG: attributes #[[MAIN_ATTRS]] = {{{.*}}stackrealign{{.*}}}

int main() { return 0; }
