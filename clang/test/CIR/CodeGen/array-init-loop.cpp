// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
// RUN: %clang -std=c++17 -target x86_64-unknown-linux-gnu -S -emit-llvm -fno-exceptions -fclangir %s -o /dev/null

// Structured bindings with arrays trigger ArrayInitLoopExpr.

int test_basic() {
  int a[] = {1, 2, 3};
  auto [x, y, z] = a;
  return x + z;
}

// CIR-LABEL: cir.func {{.*}} @_Z10test_basicv()
// CIR:   %[[A:.*]] = cir.alloca !cir.array<!s32i x 3>, !cir.ptr<!cir.array<!s32i x 3>>, ["a", init]
// CIR:   %[[DECOMP:.*]] = cir.alloca !cir.array<!s32i x 3>, !cir.ptr<!cir.array<!s32i x 3>>, ["", init]
// CIR:   %[[INDEX_ADDR:.*]] = cir.alloca !s64i, !cir.ptr<!s64i>, ["arrayinit.index"]
// CIR:   cir.copy {{.*}} to %[[A]]
// CIR:   %[[BEGIN:.*]] = cir.cast array_to_ptrdecay %[[DECOMP]]
// CIR:   %[[ZERO:.*]] = cir.const #cir.int<0> : !s64i
// CIR:   cir.store{{.*}} %[[ZERO]], %[[INDEX_ADDR]]
// CIR:   %[[BOUND:.*]] = cir.const #cir.int<3> : !s64i
// CIR:   cir.do {
// CIR:     %[[IDX:.*]] = cir.load{{.*}} %[[INDEX_ADDR]]
// CIR:     %[[DEST:.*]] = cir.ptr_stride inbounds %[[BEGIN]], %[[IDX]]
// CIR:     %[[SRC:.*]] = cir.get_element %[[A]][%[[IDX]] : !s64i]
// CIR:     %[[VAL:.*]] = cir.load{{.*}} %[[SRC]]
// CIR:     cir.store{{.*}} %[[VAL]], %[[DEST]]
// CIR:     %[[ONE:.*]] = cir.const #cir.int<1> : !s64i
// CIR:     %[[NEXT:.*]] = cir.binop(add, %[[IDX]], %[[ONE]]) : !s64i
// CIR:     cir.store{{.*}} %[[NEXT]], %[[INDEX_ADDR]]
// CIR:     cir.yield
// CIR:   } while {
// CIR:     %[[CUR:.*]] = cir.load{{.*}} %[[INDEX_ADDR]]
// CIR:     %[[CMP:.*]] = cir.cmp(ne, %[[CUR]], %[[BOUND]]) : !s64i, !cir.bool
// CIR:     cir.condition(%[[CMP]])
// CIR:   }

// LLVM-LABEL: define {{.*}} @_Z10test_basicv()
// LLVM:   %[[A:.*]] = alloca [3 x i32]
// LLVM:   %[[DECOMP:.*]] = alloca [3 x i32]
// LLVM:   %[[INDEX:.*]] = alloca i64
// LLVM:   store i64 0, ptr %[[INDEX]]
// LLVM:   br label %[[BODY:.*]]
// LLVM: [[COND:[0-9]+]]:
// LLVM:   %[[IDX_COND:.*]] = load i64, ptr %[[INDEX]]
// LLVM:   %[[DONE:.*]] = icmp ne i64 %[[IDX_COND]], 3
// LLVM:   br i1 %[[DONE]], label %[[BODY]], label %[[END:.*]]
// LLVM: [[BODY]]:
// LLVM:   %[[IDX_BODY:.*]] = load i64, ptr %[[INDEX]]
// LLVM:   getelementptr inbounds i32, ptr %{{.*}}, i64 %[[IDX_BODY]]
// LLVM:   getelementptr inbounds [3 x i32], ptr %[[A]], i32 0, i64 %[[IDX_BODY]]
// LLVM:   store i64 %{{.*}}, ptr %[[INDEX]]
// LLVM:   br label %[[COND]]

// OGCG-LABEL: define {{.*}} @_Z10test_basicv()
// OGCG:   %[[A:.*]] = alloca [3 x i32]
// OGCG:   %[[DECOMP:.*]] = alloca [3 x i32]
// OGCG:   call void @llvm.memcpy

struct NonTrivial {
  int value;
  NonTrivial() : value(1) {}
  NonTrivial(const NonTrivial &) = default;
  ~NonTrivial() {}
};

// Destructed array elements plus -fno-exceptions used to crash in
// VisitArrayInitLoopExpr by deactivating a cleanup that was never pushed.
int test_nontrivial_noexcept() {
  NonTrivial a[2];
  auto [x, y] = a;
  return x.value + y.value;
}
