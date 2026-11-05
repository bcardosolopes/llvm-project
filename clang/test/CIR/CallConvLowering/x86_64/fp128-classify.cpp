// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// __float128 must be classified as (SSE, SSEUp) on x86_64 SysV and use fp128
// as its ABI register type, matching OGCG.

using f128 = __float128;

struct Pair {
  f128 x;
  int y;
};

// CIR-LABEL: define{{.*}} @_Z9take_f128g(fp128 noundef %{{.*}})
// OGCG-LABEL: define{{.*}} @_Z9take_f128g(fp128 noundef %{{.*}})
void take_f128(f128 x) {}

// CIR-LABEL: define{{.*}}fp128 @_Z8ret_f128v()
// OGCG-LABEL: define{{.*}}fp128 @_Z8ret_f128v()
f128 ret_f128() { return 0; }

// CIR-LABEL: define{{.*}} @_Z9take_pair4Pair(ptr noundef byval(%struct.Pair) align 16 %{{.*}})
// OGCG-LABEL: define{{.*}} @_Z9take_pair4Pair(ptr noundef byval(%struct.Pair) align 16 %{{.*}})
void take_pair(Pair p) {}

// CIR-LABEL: define{{.*}} @_Z8ret_pairv(ptr dead_on_unwind noalias writable sret(%struct.Pair) align 16 %{{.*}})
// OGCG-LABEL: define{{.*}} @_Z8ret_pairv(ptr dead_on_unwind noalias writable sret(%struct.Pair) align 16 %{{.*}})
Pair ret_pair() { return {}; }
