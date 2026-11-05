// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-plt -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-plt -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that -fno-plt adds nonlazybind to extern function declarations
// but not to defined functions.

extern void bar(void);
void foo(void) { bar(); }

// Both CIR and OGCG should add nonlazybind to the declaration of bar.
// LLVM-DAG: declare void @bar() {{.*}}#[[DECLATTR:[0-9]+]]
// OGCG-DAG: declare void @bar() {{.*}}#[[DECLATTR:[0-9]+]]

// Neither should add nonlazybind to the definition of foo.
// LLVM-DAG: define {{.*}}void @foo() {{.*}}#[[DEFATTR:[0-9]+]]
// OGCG-DAG: define {{.*}}void @foo() {{.*}}#[[DEFATTR:[0-9]+]]

// LLVM: attributes #[[DECLATTR]] = { nonlazybind
// OGCG: attributes #[[DECLATTR]] = { nonlazybind
