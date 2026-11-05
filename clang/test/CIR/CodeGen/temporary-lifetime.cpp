// RUN: %clang_cc1 -std=c++11 -O1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c++11 -O1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// Lifetime markers for C++ temporaries.

struct A {
  A();
  ~A();
  char a[1024];
};

template <typename T>
void Foo(T &&);

void Test1() {
  Foo<const A &>(A());
  Foo<const A &>(A());
}

// CIR-LABEL: @_Z5Test1v
// CIR: cir.lifetime.start
// CIR: cir.call @_ZN1AC1Ev
// CIR: cir.lifetime.end
// CIR: cir.lifetime.start
// CIR: cir.call @_ZN1AC1Ev
// CIR: cir.lifetime.end

// LLVM-LABEL: @_Z5Test1v
// LLVM: call void @llvm.lifetime.start
// LLVM: call void @_ZN1AC1Ev
// LLVM: call void @llvm.lifetime.end
// LLVM: call void @llvm.lifetime.start
// LLVM: call void @_ZN1AC1Ev
// LLVM: call void @llvm.lifetime.end
