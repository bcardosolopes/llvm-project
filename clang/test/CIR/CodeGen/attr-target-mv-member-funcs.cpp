// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that ifunc member dispatch calls preserve this-pointer callsite
// attributes (nonnull, align, dereferenceable).

struct S {
  int __attribute__((target("sse4.2"))) foo(int) { return 0; }
  int __attribute__((target("arch=sandybridge"))) foo(int);
  int __attribute__((target("arch=ivybridge"))) foo(int) { return 1; }
  int __attribute__((target("default"))) foo(int) { return 2; }

  S &__attribute__((target("arch=ivybridge"))) operator=(const S &) {
    return *this;
  }
  S &__attribute__((target("default"))) operator=(const S &) {
    return *this;
  }
};

struct ConvertTo {
  __attribute__((target("arch=ivybridge"))) operator S() const {
    return S{};
  }
  __attribute__((target("default"))) operator S() const {
    return S{};
  }
};

int bar() {
  S s;
  S s2;
  s2 = s;

  ConvertTo C;
  s2 = static_cast<S>(C);

  return s.foo(0);
}

// CIR-DAG: cir.func weak_odr {{.*}} @_ZN1SaSERKS_.ifunc({{.*}}{cir.align = 1 : i64, cir.dereferenceable = 1 : i64, cir.nonnull, cir.noundef}
// CIR-DAG: cir.func weak_odr {{.*}} @_ZNK9ConvertTocv1SEv.ifunc({{.*}}{cir.align = 1 : i64, cir.dereferenceable = 1 : i64, cir.nonnull, cir.noundef}
// CIR-DAG: cir.func weak_odr {{.*}} @_ZN1S3fooEi.ifunc({{.*}}{cir.align = 1 : i64, cir.dereferenceable = 1 : i64, cir.nonnull, cir.noundef}

// LLVM: call noundef nonnull align 1 dereferenceable(1) ptr @_ZN1SaSERKS_.ifunc(ptr noundef nonnull align 1 dereferenceable(1)
// LLVM: call void @_ZNK9ConvertTocv1SEv.ifunc(ptr noundef nonnull align 1 dereferenceable(1)
// LLVM: call noundef nonnull align 1 dereferenceable(1) ptr @_ZN1SaSERKS_.ifunc(ptr noundef nonnull align 1 dereferenceable(1)
// LLVM: call noundef i32 @_ZN1S3fooEi.ifunc(ptr noundef nonnull align 1 dereferenceable(1)

// OGCG: call noundef nonnull align 1 dereferenceable(1) ptr @_ZN1SaSERKS_.ifunc(ptr noundef nonnull align 1 dereferenceable(1)
// OGCG: call void @_ZNK9ConvertTocv1SEv.ifunc(ptr noundef nonnull align 1 dereferenceable(1)
// OGCG: call noundef nonnull align 1 dereferenceable(1) ptr @_ZN1SaSERKS_.ifunc(ptr noundef nonnull align 1 dereferenceable(1)
// OGCG: call noundef i32 @_ZN1S3fooEi.ifunc(ptr noundef nonnull align 1 dereferenceable(1)
