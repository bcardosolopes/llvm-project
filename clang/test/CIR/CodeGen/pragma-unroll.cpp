// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

void test_pragma_unroll(int *List, int Length) {
  #pragma unroll
  for (int i = 0; i < Length; i++)
    List[i] = i * 2;
}

// CIR: cir.func{{.*}} @_Z18test_pragma_unroll
// CIR:   cir.for : cond
// CIR:   } {loopHint = {mustprogress = true, unroll_enable = true}}

// LLVM: define{{.*}} void @_Z18test_pragma_unroll
// LLVM:   br label %{{.*}}, !llvm.loop ![[LOOP_UNROLL:.*]]

// OGCG: define{{.*}} void @_Z18test_pragma_unroll
// OGCG:   br label %{{.*}}, !llvm.loop ![[OGCG_UNROLL:.*]]

void test_pragma_unroll_count(int *List, int Length) {
  #pragma unroll 4
  for (int i = 0; i < Length; i++)
    List[i] = i * 2;
}

// CIR: cir.func{{.*}} @_Z24test_pragma_unroll_count
// CIR:   cir.for : cond
// CIR:   } {loopHint = {mustprogress = true, unroll_count = 4 : i32}}

// LLVM: define{{.*}} void @_Z24test_pragma_unroll_count
// LLVM:   br label %{{.*}}, !llvm.loop ![[LOOP_UNROLL4:.*]]

// OGCG: define{{.*}} void @_Z24test_pragma_unroll_count
// OGCG:   br label %{{.*}}, !llvm.loop ![[OGCG_UNROLL4:.*]]

void test_pragma_nounroll(int *List, int Length) {
  #pragma nounroll
  for (int i = 0; i < Length; i++)
    List[i] = i * 2;
}

// CIR: cir.func{{.*}} @_Z20test_pragma_nounroll
// CIR:   cir.for : cond
// CIR:   } {loopHint = {mustprogress = true, unroll_disable = true}}

// LLVM: define{{.*}} void @_Z20test_pragma_nounroll
// LLVM:   br label %{{.*}}, !llvm.loop ![[LOOP_NOUNROLL:.*]]

// OGCG: define{{.*}} void @_Z20test_pragma_nounroll
// OGCG:   br label %{{.*}}, !llvm.loop ![[OGCG_NOUNROLL:.*]]

void test_pragma_unroll_full(int *a, int n) {
  #pragma clang loop unroll(full)
  for (int i = 0; i < n; i++)
    a[i] = 0;
}

// CIR: cir.func{{.*}} @_Z23test_pragma_unroll_full
// CIR:   cir.for : cond
// CIR:   } {loopHint = {mustprogress = true, unroll_full = true}}

// LLVM: define{{.*}} void @_Z23test_pragma_unroll_full
// LLVM:   br label %{{.*}}, !llvm.loop ![[LOOP_FULL:.*]]

// OGCG: define{{.*}} void @_Z23test_pragma_unroll_full
// OGCG:   br label %{{.*}}, !llvm.loop ![[OGCG_FULL:.*]]

void test_pragma_vectorize(int *a, int n) {
  #pragma clang loop vectorize(enable)
  for (int i = 0; i < n; i++)
    a[i] = 0;
}

// CIR: cir.func{{.*}} @_Z21test_pragma_vectorize
// CIR:   cir.for : cond
// CIR:   } {loopHint = {mustprogress = true, vectorize_enable = true}}

// LLVM: define{{.*}} void @_Z21test_pragma_vectorize
// LLVM:   br label %{{.*}}, !llvm.loop ![[LOOP_VEC:.*]]

// OGCG: define{{.*}} void @_Z21test_pragma_vectorize
// OGCG:   br label %{{.*}}, !llvm.loop ![[OGCG_VEC:.*]]

void test_pragma_interleave(int *a, int n) {
  #pragma clang loop interleave_count(4)
  for (int i = 0; i < n; i++)
    a[i] = 0;
}

// CIR: cir.func{{.*}} @_Z22test_pragma_interleave
// CIR:   cir.for : cond
// CIR:   } {loopHint = {interleave_count = 4 : i32, mustprogress = true}}

// LLVM: define{{.*}} void @_Z22test_pragma_interleave
// LLVM:   br label %{{.*}}, !llvm.loop ![[LOOP_IL:.*]]

// OGCG: define{{.*}} void @_Z22test_pragma_interleave
// OGCG:   br label %{{.*}}, !llvm.loop ![[OGCG_IL:.*]]

void test_pragma_vectorize_width_enable(int *a, int n) {
  #pragma clang loop vectorize_width(4) vectorize(enable)
  for (int i = 0; i < n; i++)
    a[i] = 0;
}

// CIR: cir.func{{.*}} @_Z34test_pragma_vectorize_width_enable
// CIR:   cir.for : cond
// CIR:   } {loopHint = {mustprogress = true, vectorize_enable = true, vectorize_scalable = false, vectorize_width = 4 : i32}}

// LLVM: define{{.*}} void @_Z34test_pragma_vectorize_width_enable
// LLVM:   br label %{{.*}}, !llvm.loop ![[LOOP_VEC_W_EN:.*]]

// OGCG: define{{.*}} void @_Z34test_pragma_vectorize_width_enable
// OGCG:   br label %{{.*}}, !llvm.loop ![[OGCG_VEC_W_EN:.*]]

void test_do_while_unroll(int *a, int n) {
  int i = 0;
  #pragma unroll
  do {
    a[i] = i;
    i++;
  } while (i < n);
}

// CIR: cir.func{{.*}} @_Z20test_do_while_unroll
// CIR:   cir.do {
// CIR:   } while {
// CIR:   } {loopHint = {mustprogress = true, unroll_enable = true}}

// LLVM: define{{.*}} void @_Z20test_do_while_unroll
// LLVM:   br i1 %{{.*}}, label %{{.*}}, label %{{[0-9]+}}, !llvm.loop ![[LOOP_DW_UNROLL:.*]]

// OGCG: define{{.*}} void @_Z20test_do_while_unroll
// OGCG:   br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[OGCG_DW_UNROLL:.*]]

void test_while_unroll(int *a, int n) {
  int i = 0;
  #pragma unroll
  while (i < n) {
    a[i] = i;
    i++;
  }
}

// CIR: cir.func{{.*}} @_Z17test_while_unroll
// CIR:   cir.while {
// CIR:   } {loopHint = {mustprogress = true, unroll_enable = true}}

// LLVM: define{{.*}} void @_Z17test_while_unroll
// LLVM:   br label %{{.*}}, !llvm.loop ![[LOOP_W_UNROLL:.*]]

// OGCG: define{{.*}} void @_Z17test_while_unroll
// OGCG:   br label %{{.*}}, !llvm.loop ![[OGCG_W_UNROLL:.*]]

// Verify CIR LLVM metadata definitions
// LLVM-DAG: ![[LOOP_UNROLL]] = distinct !{![[LOOP_UNROLL]], ![[MP:.*]], ![[UNROLL_EN:.*]]}
// LLVM-DAG: ![[MP]] = !{!"llvm.loop.mustprogress"}
// LLVM-DAG: ![[UNROLL_EN]] = !{!"llvm.loop.unroll.enable"}
// LLVM-DAG: ![[LOOP_UNROLL4]] = distinct !{![[LOOP_UNROLL4]], ![[MP]], ![[UNROLL4:.*]]}
// LLVM-DAG: ![[UNROLL4]] = !{!"llvm.loop.unroll.count", i32 4}
// LLVM-DAG: ![[LOOP_NOUNROLL]] = distinct !{![[LOOP_NOUNROLL]], ![[MP]], ![[NOUNROLL:.*]]}
// LLVM-DAG: ![[NOUNROLL]] = !{!"llvm.loop.unroll.disable"}
// LLVM-DAG: ![[LOOP_FULL]] = distinct !{![[LOOP_FULL]], ![[MP]], ![[FULL:.*]]}
// LLVM-DAG: ![[FULL]] = !{!"llvm.loop.unroll.full"}
// LLVM-DAG: ![[LOOP_VEC]] = distinct !{![[LOOP_VEC]], ![[MP]], ![[VEC_EN:.*]]}
// LLVM-DAG: ![[VEC_EN]] = !{!"llvm.loop.vectorize.enable", i1 true}
// LLVM-DAG: ![[LOOP_IL]] = distinct !{![[LOOP_IL]], ![[MP]], ![[IL4:.*]]}
// LLVM-DAG: ![[IL4]] = !{!"llvm.loop.interleave.count", i32 4}
// LLVM-DAG: !{!"llvm.loop.vectorize.width", i32 4}
// LLVM-DAG: !{!"llvm.loop.vectorize.scalable.enable", i1 false}
// LLVM-DAG: ![[LOOP_DW_UNROLL]] = distinct !{![[LOOP_DW_UNROLL]], ![[MP]], ![[UNROLL_EN]]}
// LLVM-DAG: ![[LOOP_W_UNROLL]] = distinct !{![[LOOP_W_UNROLL]], ![[MP]], ![[UNROLL_EN]]}

// Verify OGCG metadata definitions
// OGCG-DAG: ![[OGCG_UNROLL]] = distinct !{![[OGCG_UNROLL]], ![[OGCG_MP:.*]], ![[OGCG_UNROLL_EN:.*]]}
// OGCG-DAG: ![[OGCG_MP]] = !{!"llvm.loop.mustprogress"}
// OGCG-DAG: ![[OGCG_UNROLL_EN]] = !{!"llvm.loop.unroll.enable"}
// OGCG-DAG: ![[OGCG_UNROLL4]] = distinct !{![[OGCG_UNROLL4]], ![[OGCG_MP]], ![[OGCG_UC4:.*]]}
// OGCG-DAG: ![[OGCG_UC4]] = !{!"llvm.loop.unroll.count", i32 4}
// OGCG-DAG: ![[OGCG_NOUNROLL]] = distinct !{![[OGCG_NOUNROLL]], ![[OGCG_MP]], ![[OGCG_NOUNROLL_DIS:.*]]}
// OGCG-DAG: ![[OGCG_NOUNROLL_DIS]] = !{!"llvm.loop.unroll.disable"}
// OGCG-DAG: ![[OGCG_FULL]] = distinct !{![[OGCG_FULL]], ![[OGCG_MP]], ![[OGCG_FULL_META:.*]]}
// OGCG-DAG: ![[OGCG_FULL_META]] = !{!"llvm.loop.unroll.full"}
// OGCG-DAG: ![[OGCG_VEC]] = distinct !{![[OGCG_VEC]], ![[OGCG_MP]], ![[OGCG_VEC_EN:.*]]}
// OGCG-DAG: ![[OGCG_VEC_EN]] = !{!"llvm.loop.vectorize.enable", i1 true}
// OGCG-DAG: ![[OGCG_IL]] = distinct !{![[OGCG_IL]], ![[OGCG_MP]], ![[OGCG_IL4:.*]]}
// OGCG-DAG: ![[OGCG_IL4]] = !{!"llvm.loop.interleave.count", i32 4}
// OGCG-DAG: !{!"llvm.loop.vectorize.width", i32 4}
// OGCG-DAG: !{!"llvm.loop.vectorize.scalable.enable", i1 false}
// OGCG-DAG: ![[OGCG_DW_UNROLL]] = distinct !{![[OGCG_DW_UNROLL]], ![[OGCG_MP]], ![[OGCG_UNROLL_EN]]}
// OGCG-DAG: ![[OGCG_W_UNROLL]] = distinct !{![[OGCG_W_UNROLL]], ![[OGCG_MP]], ![[OGCG_UNROLL_EN]]}
