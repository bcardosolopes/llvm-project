// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

// TLS wrapper functions should have noinline + nounwind + target attributes,
// matching OGCG's SetLLVMFunctionAttributesForDefinition(nullptr, Wrapper).

thread_local int tls_var = 42;
int get_tls() { return tls_var; }

// CIR: cir.func no_inline comdat weak_odr private hidden @_ZTW7tls_var

// LLVM: define weak_odr hidden noundef ptr @_ZTW7tls_var() [[WRAPPER_ATTRS:#[0-9]+]] comdat
// LLVM: attributes [[WRAPPER_ATTRS]] = { noinline nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+cx8,+mmx,+sse,+sse2,+x87" }

// OGCG: define weak_odr hidden noundef ptr @_ZTW7tls_var() [[WRAPPER_ATTRS:#[0-9]+]] comdat
// OGCG: attributes [[WRAPPER_ATTRS]] = { noinline nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+cx8,+mmx,+sse,+sse2,+x87" }
