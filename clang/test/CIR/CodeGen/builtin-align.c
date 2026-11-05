// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG

// Test __builtin_align_up, __builtin_align_down, __builtin_is_aligned.

// --- Pointer operands ---

// CIR-LABEL: @align_up_ptr
// CIR: %[[MASK:.*]] = cir.binop(sub, %{{.*}}, %{{.*}}) : !u64i
// CIR: %[[GEP:.*]] = cir.ptr_stride inbounds %{{.*}}, %[[MASK]]
// CIR: %[[NOT:.*]] = cir.unary(not, %[[MASK]]) : !u64i
// CIR: cir.ptr_mask %[[GEP]], %[[NOT]]
void *align_up_ptr(void *p, unsigned long align) {
  return __builtin_align_up(p, align);
}

// LLVM-LABEL: @align_up_ptr
// LLVM: %[[MASK:.*]] = sub i64 %{{.*}}, 1
// LLVM: %[[GEP:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %[[MASK]]
// LLVM: %[[NOT:.*]] = xor i64 %[[MASK]], -1
// LLVM: call ptr @llvm.ptrmask.p0.i64(ptr %[[GEP]], i64 %[[NOT]])

// OGCG-LABEL: @align_up_ptr
// OGCG: %[[MASK:.*]] = sub i64 %{{.*}}, 1
// OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 %[[MASK]]
// OGCG: xor i64 %[[MASK]], -1
// OGCG: call ptr @llvm.ptrmask

// CIR-LABEL: @align_down_ptr
// CIR: %[[MASK:.*]] = cir.binop(sub, %{{.*}}, %{{.*}}) : !u64i
// CIR: %[[NOT:.*]] = cir.unary(not, %[[MASK]]) : !u64i
// CIR: cir.ptr_mask %{{.*}}, %[[NOT]]
void *align_down_ptr(void *p, unsigned long align) {
  return __builtin_align_down(p, align);
}

// LLVM-LABEL: @align_down_ptr
// LLVM: %[[MASK:.*]] = sub i64 %{{.*}}, 1
// LLVM: %[[NOT:.*]] = xor i64 %[[MASK]], -1
// LLVM: call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 %[[NOT]])

// CIR-LABEL: @is_aligned_ptr
// CIR: %[[MASK:.*]] = cir.binop(sub, %{{.*}}, %{{.*}}) : !u64i
// CIR: %[[INT:.*]] = cir.cast ptr_to_int %{{.*}} : !cir.ptr<!void> -> !u64i
// CIR: %[[AND:.*]] = cir.binop(and, %[[INT]], %[[MASK]]) : !u64i
// CIR: cir.cmp(eq, %[[AND]], %{{.*}}) : !u64i, !cir.bool
_Bool is_aligned_ptr(void *p, unsigned long align) {
  return __builtin_is_aligned(p, align);
}

// LLVM-LABEL: @is_aligned_ptr
// LLVM: %[[MASK:.*]] = sub i64 %{{.*}}, 1
// LLVM: %[[INT:.*]] = ptrtoint ptr %{{.*}} to i64
// LLVM: %[[AND:.*]] = and i64 %[[INT]], %[[MASK]]
// LLVM: icmp eq i64 %[[AND]], 0

// OGCG-LABEL: @is_aligned_ptr
// OGCG: %[[MASK:.*]] = sub i64 %{{.*}}, 1
// OGCG: %[[INT:.*]] = ptrtoint ptr %{{.*}} to i64
// OGCG: %[[AND:.*]] = and i64 %[[INT]], %[[MASK]]
// OGCG: icmp eq i64 %[[AND]], 0

// --- Integer operands ---

// CIR-LABEL: @align_up_int
// CIR: %[[MASK:.*]] = cir.binop(sub, %{{.*}}, %{{.*}}) : !u64i
// CIR: %[[ADD:.*]] = cir.binop(add, %{{.*}}, %[[MASK]]) : !u64i
// CIR: %[[NOT:.*]] = cir.unary(not, %[[MASK]]) : !u64i
// CIR: cir.binop(and, %[[ADD]], %[[NOT]]) : !u64i
unsigned long align_up_int(unsigned long v, unsigned long align) {
  return __builtin_align_up(v, align);
}

// LLVM-LABEL: @align_up_int
// LLVM: %[[MASK:.*]] = sub i64 %{{.*}}, 1
// LLVM: %[[ADD:.*]] = add i64 %{{.*}}, %[[MASK]]
// LLVM: %[[NOT:.*]] = xor i64 %[[MASK]], -1
// LLVM: and i64 %[[ADD]], %[[NOT]]

// OGCG-LABEL: @align_up_int
// OGCG: sub i64 %{{.*}}, 1
// OGCG: add i64 %{{.*}}, %{{.*}}
// OGCG: xor i64 %{{.*}}, -1
// OGCG: and i64

// CIR-LABEL: @align_down_int
// CIR: %[[MASK:.*]] = cir.binop(sub, %{{.*}}, %{{.*}}) : !u64i
// CIR: %[[NOT:.*]] = cir.unary(not, %[[MASK]]) : !u64i
// CIR: cir.binop(and, %{{.*}}, %[[NOT]]) : !u64i
unsigned long align_down_int(unsigned long v, unsigned long align) {
  return __builtin_align_down(v, align);
}

// LLVM-LABEL: @align_down_int
// LLVM: %[[MASK:.*]] = sub i64 %{{.*}}, 1
// LLVM: %[[NOT:.*]] = xor i64 %[[MASK]], -1
// LLVM: and i64 %{{.*}}, %[[NOT]]

// OGCG-LABEL: @align_down_int
// OGCG: sub i64 %{{.*}}, 1
// OGCG: xor i64 %{{.*}}, -1
// OGCG: and i64

// CIR-LABEL: @is_aligned_int
// CIR: %[[MASK:.*]] = cir.binop(sub, %{{.*}}, %{{.*}}) : !u64i
// CIR: %[[AND:.*]] = cir.binop(and, %{{.*}}, %[[MASK]]) : !u64i
// CIR: cir.cmp(eq, %[[AND]], %{{.*}}) : !u64i, !cir.bool
_Bool is_aligned_int(unsigned long v, unsigned long align) {
  return __builtin_is_aligned(v, align);
}

// LLVM-LABEL: @is_aligned_int
// LLVM: %[[MASK:.*]] = sub i64 %{{.*}}, 1
// LLVM: %[[AND:.*]] = and i64 %{{.*}}, %[[MASK]]
// LLVM: icmp eq i64 %[[AND]], 0

// OGCG-LABEL: @is_aligned_int
// OGCG: sub i64 %{{.*}}, 1
// OGCG: and i64 %{{.*}}, %{{.*}}
// OGCG: icmp eq i64 %{{.*}}, 0
