// The record ABI table follows anonymous-record canonicalization.
//
// cir.record_abi is keyed by record name, and the merge canonicalizes every
// anonymous record (anon.0 becomes anon.m0), so the table has to follow the
// rename. It did not: the key stayed at the input spelling, the lookup for
// the canonical name missed, and lowering fell back to its default
// convention. The bodies were generated for the table's convention, so the
// mismatch double-destroyed a by-value lambda at runtime.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/lambda.cpp -o %t/lambda.o
//
// The table key is the canonical name, not the input spelling.
// RUN: cir-link %t/lambda.o -o %t/merged.cir
// RUN: cir-opt %t/merged.cir | FileCheck %s --check-prefix=FACTS
// FACTS: cir.record_abi = {anon.m0 = {{[0-9]+}} : i32}
//
// cir-link and clang lowering the TU itself agree: the lambda goes by plain
// pointer, not byval.
// RUN: cir-link %t/lambda.o --emit-llvm --emit-llvm-text -o %t/linked.ll
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN: -O1 -S -emit-llvm %t/lambda.cpp -o %t/direct.ll
// RUN: FileCheck --input-file=%t/linked.ll %s --check-prefix=LLVM
// RUN: FileCheck --input-file=%t/direct.ll %s --check-prefix=LLVM
// LLVM-DAG: define {{.*}} @"_Z5applyIZ3runvE3$_0EiT_"(ptr noundef {{(nonnull readonly captures\(none\) )?}}%0)
// LLVM-DAG: {{invoke|call}} {{.*}} @"_Z5applyIZ3runvE3$_0EiT_"(ptr noundef %{{[0-9]+}})

//--- lambda.cpp
struct Payload {
  int d[8];
  ~Payload() {}
};
template <typename F> __attribute__((noinline)) int apply(F f) { return f(); }
int run() {
  Payload p{};
  auto l = [p]() { return p.d[0] + 1; };
  return apply(l);
}
