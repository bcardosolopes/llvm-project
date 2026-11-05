// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

void scoped_atomic_load(int *ptr) {
  // CIR-LABEL: @scoped_atomic_load
  // LLVM-LABEL: @scoped_atomic_load
  // OGCG-LABEL: @scoped_atomic_load

  int x;
  __scoped_atomic_load(ptr, &x, __ATOMIC_RELAXED, __MEMORY_SCOPE_SINGLE);
  // CIR: %{{.+}} = cir.load align(4) syncscope(single_thread) atomic(relaxed) %{{.+}} : !cir.ptr<!s32i>, !s32i
  // LLVM: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4

  __scoped_atomic_load(ptr, &x, __ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM);
  // CIR: %{{.+}} = cir.load align(4) syncscope(system) atomic(relaxed) %{{.+}} : !cir.ptr<!s32i>, !s32i
  // LLVM: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4

  __scoped_atomic_load(ptr, &x, __ATOMIC_RELAXED, __MEMORY_SCOPE_DEVICE);
  // CIR: %{{.+}} = cir.load align(4) syncscope(device) atomic(relaxed) %{{.+}} : !cir.ptr<!s32i>, !s32i
  // LLVM: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4

  __scoped_atomic_load(ptr, &x, __ATOMIC_RELAXED, __MEMORY_SCOPE_WRKGRP);
  // CIR: %{{.+}} = cir.load align(4) syncscope(workgroup) atomic(relaxed) %{{.+}} : !cir.ptr<!s32i>, !s32i
  // LLVM: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4

  __scoped_atomic_load(ptr, &x, __ATOMIC_RELAXED, __MEMORY_SCOPE_CLUSTR);
  // CIR: %{{.+}} = cir.load align(4) syncscope(cluster) atomic(relaxed) %{{.+}} : !cir.ptr<!s32i>, !s32i
  // LLVM: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4

  __scoped_atomic_load(ptr, &x, __ATOMIC_RELAXED, __MEMORY_SCOPE_WVFRNT);
  // CIR: %{{.+}} = cir.load align(4) syncscope(wavefront) atomic(relaxed) %{{.+}} : !cir.ptr<!s32i>, !s32i
  // LLVM: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
}

void scoped_atomic_load_n(int *ptr) {
  // CIR-LABEL: @scoped_atomic_load_n
  // LLVM-LABEL: @scoped_atomic_load_n
  // OGCG-LABEL: @scoped_atomic_load_n

  int x;
  x = __scoped_atomic_load_n(ptr, __ATOMIC_RELAXED, __MEMORY_SCOPE_SINGLE);
  // CIR: %{{.+}} = cir.load align(4) syncscope(single_thread) atomic(relaxed) %{{.+}} : !cir.ptr<!s32i>, !s32i
  // LLVM: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4

  x = __scoped_atomic_load_n(ptr, __ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM);
  // CIR: %{{.+}} = cir.load align(4) syncscope(system) atomic(relaxed) %{{.+}} : !cir.ptr<!s32i>, !s32i
  // LLVM: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = load atomic i32, ptr %{{.+}} monotonic, align 4
}

void scoped_atomic_store(int *ptr, int value) {
  // CIR-LABEL: @scoped_atomic_store
  // LLVM-LABEL: @scoped_atomic_store
  // OGCG-LABEL: @scoped_atomic_store

  __scoped_atomic_store(ptr, &value, __ATOMIC_RELAXED, __MEMORY_SCOPE_SINGLE);
  // CIR: cir.store align(4) syncscope(single_thread) atomic(relaxed) %{{.+}}, %{{.+}} : !s32i, !cir.ptr<!s32i>
  // LLVM: store atomic i32 %{{.+}}, ptr %{{.+}} monotonic, align 4
  // OGCG: store atomic i32 %{{.+}}, ptr %{{.+}} monotonic, align 4

  __scoped_atomic_store(ptr, &value, __ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM);
  // CIR: cir.store align(4) syncscope(system) atomic(relaxed) %{{.+}}, %{{.+}} : !s32i, !cir.ptr<!s32i>
  // LLVM: store atomic i32 %{{.+}}, ptr %{{.+}} monotonic, align 4
  // OGCG: store atomic i32 %{{.+}}, ptr %{{.+}} monotonic, align 4
}

void scoped_atomic_store_n(int *ptr, int value) {
  // CIR-LABEL: @scoped_atomic_store_n
  // LLVM-LABEL: @scoped_atomic_store_n
  // OGCG-LABEL: @scoped_atomic_store_n

  __scoped_atomic_store_n(ptr, value, __ATOMIC_RELAXED, __MEMORY_SCOPE_SINGLE);
  // CIR: cir.store align(4) syncscope(single_thread) atomic(relaxed) %{{.+}}, %{{.+}} : !s32i, !cir.ptr<!s32i>
  // LLVM: store atomic i32 %{{.+}}, ptr %{{.+}} monotonic, align 4
  // OGCG: store atomic i32 %{{.+}}, ptr %{{.+}} monotonic, align 4

  __scoped_atomic_store_n(ptr, value, __ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM);
  // CIR: cir.store align(4) syncscope(system) atomic(relaxed) %{{.+}}, %{{.+}} : !s32i, !cir.ptr<!s32i>
  // LLVM: store atomic i32 %{{.+}}, ptr %{{.+}} monotonic, align 4
  // OGCG: store atomic i32 %{{.+}}, ptr %{{.+}} monotonic, align 4
}

int scoped_atomic_fetch_add(int *ptr) {
  // CIR-LABEL: @scoped_atomic_fetch_add
  // LLVM-LABEL: @scoped_atomic_fetch_add
  // OGCG-LABEL: @scoped_atomic_fetch_add

  int x;
  x = __scoped_atomic_fetch_add(ptr, 1, __ATOMIC_RELAXED, __MEMORY_SCOPE_SINGLE);
  // CIR: %{{.+}} = cir.atomic.fetch add relaxed fetch_first %{{.+}}, %{{.+}} syncscope(single_thread) : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // LLVM: %{{.+}} = atomicrmw add ptr %{{.+}}, i32 %{{.+}} syncscope("singlethread") monotonic, align 4
  // OGCG: %{{.+}} = atomicrmw add ptr %{{.+}}, i32 %{{.+}} monotonic, align 4

  x = __scoped_atomic_fetch_sub(ptr, 1, __ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM);
  // CIR: %{{.+}} = cir.atomic.fetch sub relaxed fetch_first %{{.+}}, %{{.+}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // LLVM: %{{.+}} = atomicrmw sub ptr %{{.+}}, i32 %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = atomicrmw sub ptr %{{.+}}, i32 %{{.+}} monotonic, align 4

  return x;
}

unsigned scoped_atomic_fetch_uinc(unsigned *ptr) {
  // CIR-LABEL: @scoped_atomic_fetch_uinc
  // LLVM-LABEL: @scoped_atomic_fetch_uinc
  // OGCG-LABEL: @scoped_atomic_fetch_uinc

  unsigned x;
  x = __scoped_atomic_fetch_uinc(ptr, ~0u, __ATOMIC_RELAXED,
                                __MEMORY_SCOPE_SINGLE);
  // CIR: %{{.+}} = cir.atomic.fetch uinc_wrap relaxed fetch_first %{{.+}}, %{{.+}} syncscope(single_thread) : (!cir.ptr<!u32i>, !u32i) -> !u32i
  // LLVM: %{{.+}} = atomicrmw uinc_wrap ptr %{{.+}}, i32 %{{.+}} syncscope("singlethread") monotonic, align 4
  // OGCG: %{{.+}} = atomicrmw uinc_wrap ptr %{{.+}}, i32 %{{.+}} monotonic, align 4

  x = __scoped_atomic_fetch_udec(ptr, ~0u, __ATOMIC_RELAXED,
                                __MEMORY_SCOPE_SYSTEM);
  // CIR: %{{.+}} = cir.atomic.fetch udec_wrap relaxed fetch_first %{{.+}}, %{{.+}} : (!cir.ptr<!u32i>, !u32i) -> !u32i
  // LLVM: %{{.+}} = atomicrmw udec_wrap ptr %{{.+}}, i32 %{{.+}} monotonic, align 4
  // OGCG: %{{.+}} = atomicrmw udec_wrap ptr %{{.+}}, i32 %{{.+}} monotonic, align 4

  return x;
}
