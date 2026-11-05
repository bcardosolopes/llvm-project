// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Check that the preserve_most calling convention attribute at the source level
// is lowered to the corresponding calling convention attribute in CIR and LLVM.
void foo(void) __attribute__((preserve_most)) {
  // CIR: cir.func {{.*}} @foo() cc(preserve_most)
  // LLVM: define {{(dso_local )?}}preserve_mostcc void @foo()
}

// Check that the preserve_all calling convention attribute at the source level
// is lowered to the corresponding calling convention attribute in CIR and LLVM.
void boo(void) __attribute__((preserve_all)) {
  // CIR: cir.func {{.*}} @boo() cc(preserve_all)
  // LLVM: define {{(dso_local )?}}preserve_allcc void @boo()
}

// Check that the preserve_none calling convention attribute at the source level
// is lowered to the corresponding calling convention attribute in CIR and LLVM.
void bar(void) __attribute__((preserve_none)) {
  // CIR: cir.func {{.*}} @bar() cc(preserve_none)
  // LLVM: define {{(dso_local )?}}preserve_nonecc void @bar()
}
