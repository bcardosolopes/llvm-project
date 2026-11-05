// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=BEFORE
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: cir-opt %t.flat.cir --mem2reg -o %t.mem2reg.cir
// RUN: FileCheck --input-file=%t.mem2reg.cir %s -check-prefix=MEM2REG

int return_42() {
  int y = 42;
  return y;
}

// BEFORE: cir.func {{.*@return_42}}
// BEFORE:   %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["y", init] {alignment = 4 : i64}
// BEFORE:   %1 = cir.const #cir.int<42> : !s32i
// BEFORE:   cir.store{{.*}} %1, %0 : !s32i, !cir.ptr<!s32i>
// BEFORE:   %2 = cir.load{{.*}} %0 : !cir.ptr<!s32i>, !s32i
// BEFORE:   cir.return %2 : !s32i

// MEM2REG:  cir.func {{.*@return_42()}}
// MEM2REG:    %0 = cir.const #cir.int<42> : !s32i
// MEM2REG:    cir.return %0 : !s32i

void alloca_in_loop(int* ar, int n) {
  for (int i = 0; i < n; ++i) {
    int a = 4;
    ar[i] = a;
  }
}

// BEFORE:  cir.func {{.*@alloca_in_loop}}
// BEFORE:    %0 = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["ar", init] {alignment = 8 : i64}
// BEFORE:    %1 = cir.alloca !s32i, !cir.ptr<!s32i>, ["n", init] {alignment = 4 : i64}
// BEFORE:    cir.store{{.*}} %arg0, %0 : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// BEFORE:    cir.store{{.*}} %arg1, %1 : !s32i, !cir.ptr<!s32i>
// BEFORE:    cir.scope {
// BEFORE:      %2 = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init] {alignment = 4 : i64}
// BEFORE:      %3 = cir.const #cir.int<0> : !s32i
// BEFORE:      cir.store{{.*}} %3, %2 : !s32i, !cir.ptr<!s32i>
// BEFORE:      cir.for : cond {
// BEFORE:        %4 = cir.load{{.*}} %2 : !cir.ptr<!s32i>, !s32i
// BEFORE:        %5 = cir.load{{.*}} %1 : !cir.ptr<!s32i>, !s32i
// BEFORE:        %6 = cir.cmp(lt, %4, %5) : !s32i, !cir.bool
// BEFORE:        cir.condition(%6)
// BEFORE:      } body {
// BEFORE:        cir.scope {
// BEFORE:          %4 = cir.alloca !s32i, !cir.ptr<!s32i>, ["a", init] {alignment = 4 : i64}
// BEFORE:          %5 = cir.const #cir.int<4> : !s32i
// BEFORE:          cir.store{{.*}} %5, %4 : !s32i, !cir.ptr<!s32i>
// BEFORE:          %6 = cir.load{{.*}} %4 : !cir.ptr<!s32i>, !s32i
// BEFORE:          %7 = cir.load{{.*}} %2 : !cir.ptr<!s32i>, !s32i
// BEFORE:          %8 = cir.load{{.*}} %0 : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// BEFORE:          %9 = cir.ptr_stride inbounds %8, %7 : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
// BEFORE:          cir.store{{.*}} %6, %9 : !s32i, !cir.ptr<!s32i>
// BEFORE:        }
// BEFORE:        cir.yield
// BEFORE:      } step {
// BEFORE:        %4 = cir.load{{.*}} %2 : !cir.ptr<!s32i>, !s32i
// BEFORE:        %5 = cir.unary(inc, %4) nsw : !s32i, !s32i
// BEFORE:        cir.store{{.*}} %5, %2 : !s32i, !cir.ptr<!s32i>
// BEFORE:        cir.yield
// BEFORE:      }
// BEFORE:    }
// BEFORE:    cir.return

// MEM2REG:  cir.func {{.*@alloca_in_loop}}
// MEM2REG:    %0 = cir.const #cir.int<0> : !s32i
// MEM2REG:    cir.br ^bb1(%0 : !s32i)
// MEM2REG:  ^bb1(%1: !s32i{{.*}}):
// MEM2REG:    %2 = cir.cmp(lt, %1, %arg1) : !s32i, !cir.bool
// MEM2REG:    cir.brcond %2 ^bb2, ^bb5
// MEM2REG:  ^bb2:
// MEM2REG:    %3 = cir.const #cir.int<4> : !s32i
// MEM2REG:    %4 = cir.ptr_stride inbounds %arg0, %1 : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
// MEM2REG:    cir.store{{.*}} %3, %4 : !s32i, !cir.ptr<!s32i>
// MEM2REG:    cir.br ^bb3
// MEM2REG:  ^bb3:
// MEM2REG:    cir.br ^bb4
// MEM2REG:  ^bb4:
// MEM2REG:    %5 = cir.unary(inc, %1) nsw : !s32i, !s32i
// MEM2REG:    cir.br ^bb1(%5 : !s32i)
// MEM2REG:  ^bb5:
// MEM2REG:    cir.return

int alloca_in_ifelse(int x) {
  int y = 0;
  if (x > 42) {
    int z = 2;
    y = x * z;
  } else  {
    int z = 3;
    y = x * z;
  }

  y = y + 1;
  return y;
}

// BEFORE:  cir.func {{.*@alloca_in_ifelse}}
// BEFORE:    %[[X_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init] {alignment = 4 : i64}
// BEFORE:    %[[Y_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["y", init] {alignment = 4 : i64}
// BEFORE:    cir.store{{.*}} %arg0, %[[X_ADDR]] : !s32i, !cir.ptr<!s32i>
// BEFORE:    %[[ZERO:.*]] = cir.const #cir.int<0> : !s32i
// BEFORE:    cir.store{{.*}} %[[ZERO]], %[[Y_ADDR]] : !s32i, !cir.ptr<!s32i>
// BEFORE:      %[[LOAD:.*]] = cir.load{{.*}} %[[X_ADDR]] : !cir.ptr<!s32i>, !s32i
// BEFORE:      %[[C42:.*]] = cir.const #cir.int<42> : !s32i
// BEFORE:      %[[CMP:.*]] = cir.cmp(gt, %[[LOAD]], %[[C42]]) : !s32i, !cir.bool
// BEFORE:      cir.if %[[CMP]] {
// BEFORE:        %[[Z1:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["z", init] {alignment = 4 : i64}
// BEFORE:        %[[C2:.*]] = cir.const #cir.int<2> : !s32i
// BEFORE:        cir.store{{.*}} %[[C2]], %[[Z1]] : !s32i, !cir.ptr<!s32i>
// BEFORE:        %[[X1:.*]] = cir.load{{.*}} %[[X_ADDR]] : !cir.ptr<!s32i>, !s32i
// BEFORE:        %[[Z1V:.*]] = cir.load{{.*}} %[[Z1]] : !cir.ptr<!s32i>, !s32i
// BEFORE:        %[[MUL1:.*]] = cir.binop(mul, %[[X1]], %[[Z1V]]) nsw : !s32i
// BEFORE:        cir.store{{.*}} %[[MUL1]], %[[Y_ADDR]] : !s32i, !cir.ptr<!s32i>
// BEFORE:      } else {
// BEFORE:        %[[Z2:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["z", init] {alignment = 4 : i64}
// BEFORE:        %[[C3:.*]] = cir.const #cir.int<3> : !s32i
// BEFORE:        cir.store{{.*}} %[[C3]], %[[Z2]] : !s32i, !cir.ptr<!s32i>
// BEFORE:        %[[X2:.*]] = cir.load{{.*}} %[[X_ADDR]] : !cir.ptr<!s32i>, !s32i
// BEFORE:        %[[Z2V:.*]] = cir.load{{.*}} %[[Z2]] : !cir.ptr<!s32i>, !s32i
// BEFORE:        %[[MUL2:.*]] = cir.binop(mul, %[[X2]], %[[Z2V]]) nsw : !s32i
// BEFORE:        cir.store{{.*}} %[[MUL2]], %[[Y_ADDR]] : !s32i, !cir.ptr<!s32i>
// BEFORE:     }
// BEFORE:    %[[V1:.*]] = cir.load{{.*}} %[[Y_ADDR]] : !cir.ptr<!s32i>, !s32i
// BEFORE:    %[[V2:.*]] = cir.const #cir.int<1> : !s32i
// BEFORE:    %[[V3:.*]] = cir.binop(add, %[[V1]], %[[V2]]) nsw : !s32i
// BEFORE:    cir.store{{.*}} %[[V3]], %[[Y_ADDR]] : !s32i, !cir.ptr<!s32i>
// BEFORE:    %[[V4:.*]] = cir.load{{.*}} %[[Y_ADDR]] : !cir.ptr<!s32i>, !s32i
// BEFORE:    cir.return %[[V4]] : !s32i

// MEM2REG:  cir.func {{.*@alloca_in_ifelse}}
// MEM2REG:    %0 = cir.const #cir.int<0> : !s32i
// MEM2REG:    %1 = cir.const #cir.int<42> : !s32i
// MEM2REG:    %2 = cir.cmp(gt, %arg0, %1) : !s32i, !cir.bool
// MEM2REG:    cir.brcond %2 ^bb1, ^bb2
// MEM2REG:  ^bb1:  // pred: ^bb0
// MEM2REG:    %3 = cir.const #cir.int<2> : !s32i
// MEM2REG:    %4 = cir.binop(mul, %arg0, %3) nsw : !s32i
// MEM2REG:    cir.br ^bb3(%4 : !s32i)
// MEM2REG:  ^bb2:  // pred: ^bb0
// MEM2REG:    %5 = cir.const #cir.int<3> : !s32i
// MEM2REG:    %6 = cir.binop(mul, %arg0, %5) nsw : !s32i
// MEM2REG:    cir.br ^bb3(%6 : !s32i)
// MEM2REG:  ^bb3(%7: !s32i{{.*}}):  // 2 preds: ^bb1, ^bb2
// MEM2REG:    %8 = cir.const #cir.int<1> : !s32i
// MEM2REG:    %9 = cir.binop(add, %7, %8) nsw : !s32i
// MEM2REG:    cir.return %9 : !s32i
// MEM2REG:  }

typedef __SIZE_TYPE__ size_t;
void *alloca(size_t size);

void test_bitcast(size_t n) {
  int *c1 = alloca(n);
}

// BEFORE:  cir.func {{.*@test_bitcast}}
// BEFORE:    %0 = cir.alloca !u64i, !cir.ptr<!u64i>, ["n", init] {alignment = 8 : i64}
// BEFORE:    %1 = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["c1", init] {alignment = 8 : i64}
// BEFORE:    cir.store{{.*}} %arg0, %0 : !u64i, !cir.ptr<!u64i>
// BEFORE:    %2 = cir.load{{.*}} %0 : !cir.ptr<!u64i>, !u64i
// BEFORE:    %3 = cir.alloca !u8i, !cir.ptr<!u8i>, %2 : !u64i, ["bi_alloca"] {alignment = 16 : i64}
// BEFORE:    %4 = cir.cast bitcast %3 : !cir.ptr<!u8i> -> !cir.ptr<!void>
// BEFORE:    %5 = cir.cast bitcast %4 : !cir.ptr<!void> -> !cir.ptr<!s32i>
// BEFORE:    cir.store{{.*}} %5, %1 : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// BEFORE:    cir.return

// MEM2REG:  cir.func {{.*@test_bitcast}}
// MEM2REG:    cir.return
// MEM2REG:  }
