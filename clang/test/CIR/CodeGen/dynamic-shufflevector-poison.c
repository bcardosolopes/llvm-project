// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// Dynamic shufflevector should use poison, not undef, for the
// initial result vector.

typedef float float4 __attribute__((__vector_size__(16)));
typedef int int4 __attribute__((__vector_size__(16)));

float4 dynamic_shuffle(float4 v, int4 idx) {
  return __builtin_shufflevector(v, idx);
}

// The first insertelement should insert into poison, not undef.
// CIR-LABEL: @dynamic_shuffle
// CIR: insertelement <4 x float> poison,
// CIR-NOT: insertelement <4 x float> undef,
// OGCG-LABEL: @dynamic_shuffle
// OGCG: insertelement <4 x float> poison,
// OGCG-NOT: insertelement <4 x float> undef,
