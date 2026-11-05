// RUN: %clang_cc1 %s -triple aarch64-linux-gnu -ffreestanding -fclangir -emit-cir -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 %s -triple aarch64-linux-gnu -ffreestanding -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 %s -triple aarch64-linux-gnu -ffreestanding -emit-llvm -o - | FileCheck %s --check-prefix=OGCG

// REQUIRES: aarch64-registered-target

#include <stdatomic.h>

_Float16 atomic_max_fetch_f16(_Float16 *ptr, _Float16 value) {
  // CIR-LABEL: @atomic_max_fetch_f16
  // CIR: %{{.+}} = cir.atomic.fetch max seq_cst %{{.+}}, %{{.+}} align(2) : (!cir.ptr<!cir.f16>, !cir.f16) -> !cir.f16

  // LLVM-LABEL: @atomic_max_fetch_f16
  // LLVM: %[[OLD:.+]] = atomicrmw fmax ptr %{{.+}}, half %[[VAL:.+]] seq_cst, align 2
  // LLVM-NEXT: %[[RES:.+]] = call half @llvm.maxnum.f16(half %[[OLD]], half %[[VAL]])

  // OGCG-LABEL: @atomic_max_fetch_f16
  // OGCG: %[[OLD:.+]] = atomicrmw fmax ptr %{{.+}}, half %[[VAL:.+]] seq_cst, align 2
  // OGCG-NEXT: %[[RES:.+]] = call half @llvm.maxnum.f16(half %[[OLD]], half %[[VAL]])
  return __atomic_max_fetch(ptr, value, __ATOMIC_SEQ_CST);
}

__bf16 atomic_min_fetch_bf16(__bf16 *ptr, __bf16 value) {
  // CIR-LABEL: @atomic_min_fetch_bf16
  // CIR: %{{.+}} = cir.atomic.fetch min seq_cst %{{.+}}, %{{.+}} align(2) : (!cir.ptr<!cir.bf16>, !cir.bf16) -> !cir.bf16

  // LLVM-LABEL: @atomic_min_fetch_bf16
  // LLVM: %[[OLD:.+]] = atomicrmw fmin ptr %{{.+}}, bfloat %[[VAL:.+]] seq_cst, align 2
  // LLVM-NEXT: %[[RES:.+]] = call bfloat @llvm.minnum.bf16(bfloat %[[OLD]], bfloat %[[VAL]])

  // OGCG-LABEL: @atomic_min_fetch_bf16
  // OGCG: %[[OLD:.+]] = atomicrmw fmin ptr %{{.+}}, bfloat %[[VAL:.+]] seq_cst, align 2
  // OGCG-NEXT: %[[RES:.+]] = call bfloat @llvm.minnum.bf16(bfloat %[[OLD]], bfloat %[[VAL]])
  return __atomic_min_fetch(ptr, value, __ATOMIC_SEQ_CST);
}
