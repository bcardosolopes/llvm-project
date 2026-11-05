// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -ffreestanding -fvisibility=hidden -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -ffreestanding -fvisibility=hidden -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -ffreestanding -fvisibility=hidden -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

int scoped_fetch_add_device(int *ptr) {
  // CIR-LABEL: @scoped_fetch_add_device
  // LLVM-LABEL: @scoped_fetch_add_device
  // OGCG-LABEL: @scoped_fetch_add_device
  // CIR: = cir.atomic.fetch add relaxed fetch_first %{{.+}}, %{{.+}} syncscope(device) : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // LLVM: = atomicrmw add ptr %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic, align 4
  // OGCG: = atomicrmw add ptr %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic, align 4
  return __scoped_atomic_fetch_add(ptr, 1, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_DEVICE);
}

int scoped_fetch_sub_workgroup(int *ptr) {
  // CIR-LABEL: @scoped_fetch_sub_workgroup
  // LLVM-LABEL: @scoped_fetch_sub_workgroup
  // OGCG-LABEL: @scoped_fetch_sub_workgroup
  // CIR: = cir.atomic.fetch sub relaxed fetch_first %{{.+}}, %{{.+}} syncscope(workgroup) : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // LLVM: = atomicrmw sub ptr %{{.+}}, i32 %{{.+}} syncscope("workgroup") monotonic, align 4
  // OGCG: = atomicrmw sub ptr %{{.+}}, i32 %{{.+}} syncscope("workgroup") monotonic, align 4
  return __scoped_atomic_fetch_sub(ptr, 1, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_WRKGRP);
}

unsigned scoped_wrap_device(unsigned *ptr) {
  // CIR-LABEL: @scoped_wrap_device
  // LLVM-LABEL: @scoped_wrap_device
  // OGCG-LABEL: @scoped_wrap_device
  // CIR: = cir.atomic.fetch uinc_wrap relaxed fetch_first %{{.+}}, %{{.+}} syncscope(device) : (!cir.ptr<!u32i>, !u32i) -> !u32i
  // CIR: = cir.atomic.fetch udec_wrap relaxed fetch_first %{{.+}}, %{{.+}} syncscope(device) : (!cir.ptr<!u32i>, !u32i) -> !u32i
  // LLVM: = atomicrmw uinc_wrap ptr %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic, align 4
  // LLVM: = atomicrmw udec_wrap ptr %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic, align 4
  // OGCG: = atomicrmw uinc_wrap ptr %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic, align 4
  // OGCG: = atomicrmw udec_wrap ptr %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic, align 4
  unsigned x = __scoped_atomic_fetch_uinc(ptr, ~0u, __ATOMIC_RELAXED,
                                         __MEMORY_SCOPE_DEVICE);
  return __scoped_atomic_fetch_udec(ptr, ~0u, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_DEVICE) +
         x;
}
