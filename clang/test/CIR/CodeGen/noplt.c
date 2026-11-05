// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-cir -fno-plt %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -emit-llvm -fno-plt %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// CIR-DAG: #fn_attr{{[0-9]*}} = #cir<extra({{{.*}}nonlazybind{{.*}}})>
// CIR-DAG: module {{.*}} attributes {{.*}}cir.rtlib_use_got

int foo(void);

int bar(void) {
  return foo();
}

// LLVM: Function Attrs: nonlazybind
// LLVM-NEXT: declare {{.*}}i32 @foo
// LLVM: !"RtLibUseGOT"
