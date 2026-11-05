// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t-ogcg.ll %s

// Regression coverage for union storage-member tie-breaking when members have
// equal ABI alignment. The largest-size member must be selected, matching OGCG.

struct S16 {
  long long a;
  long long b;
};

union U {
  long long s8;
  S16 s16;
};

U g;

// LLVM: %union.U = type { %struct.S16 }
// OGCG: %union.U = type { %struct.S16 }
