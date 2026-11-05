// RUN: %clang_cc1 -triple x86_64-unknown-linux -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that virtual musttail calls preserve nonnull/align/dereferenceable
// attributes on the this pointer.

class TestVirtual {
  virtual void TailTo();
  virtual void TailFrom();
};

void TestVirtual::TailFrom() {
  [[clang::musttail]] return TailTo();
}

// CIR-LABEL: @_ZN11TestVirtual8TailFromEv
// CIR: cir.call must_tail %{{.+}}(%{{.+}}) {cir.call_arg_attrs = [{cir.align = 8 : i64, cir.dereferenceable = 8 : i64, cir.nonnull, cir.noundef}]}

// LLVM-LABEL: define dso_local void @_ZN11TestVirtual8TailFromEv
// LLVM: musttail call void %{{.+}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.+}})
