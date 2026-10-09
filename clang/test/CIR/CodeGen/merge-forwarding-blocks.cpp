// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-OPT --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O0 -disable-llvm-passes %s -o %t-o0.ll
// RUN: FileCheck --check-prefix=LLVM-O0 --input-file=%t-o0.ll %s

// Trivial forwarding blocks produced when CIR's structured control flow is
// flattened are merged into their predecessors at -O1+, before any LLVM
// passes run. At -O0 the merge is off and the raw shape is preserved.

// LLVM-OPT-LABEL: define {{.*}} @_Z11fwd_if_elseiii(
// LLVM-OPT:       [[CMP:%.*]] = icmp ne i32 %0, 0
// LLVM-OPT-NEXT:  br i1 [[CMP]], label {{%.*}}, label {{%.*}}
int fwd_if_else(int c, int a, int b) {
  if (c)
    return a;
  else
    return b;
}

// The loop header block directly follows the entry branch: the unmerged
// lowering threads the entry through a chain of empty forwarding blocks
// before reaching the header.

// LLVM-OPT-LABEL: define {{.*}} @_Z8fwd_loopi(
// LLVM-OPT:       br label {{%.*}}
// LLVM-OPT-EMPTY:
// LLVM-OPT-NEXT:  {{^[0-9]+:.*$}}
// LLVM-OPT-NEXT:  {{%.*}} = phi i32
int fwd_loop(int n) {
  int total = 0;
  for (int i = 0; i < n; ++i)
    total += i;
  return total;
}

// LLVM-O0-LABEL: define {{.*}} @_Z11fwd_if_elseiii(
// LLVM-O0:       store i32 %2, ptr {{%.*}}, align 4
// LLVM-O0-NEXT:  br label {{%.*}}
