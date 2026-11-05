// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

extern void B (void);
static __typeof(B) A __attribute__ ((__weakref__("B")));

void active (void)
{
  A();
}

// CIR-DAG:  cir.global weak_odr comdat @y
// CIR-DAG:  cir.global weak @x
// CIR:      cir.func {{.*}} @active()
// CIR-NEXT:   cir.call @B() : () -> ()
// CIR:      cir.func extern_weak private @B()

// LLVM-DAG: @y = weak_odr global
// LLVM-DAG: @x = weak global
// LLVM:     define dso_local void @active()
// LLVM-NEXT:  call void @B()
// LLVM:     declare extern_weak void @B()

int __attribute__((selectany)) y;

int __attribute__((weak)) x;
