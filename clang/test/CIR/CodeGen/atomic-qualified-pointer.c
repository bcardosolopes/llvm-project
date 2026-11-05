// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Test that _Atomic-qualified pointer types are handled correctly.
// The AtomicToNonAtomic / NonAtomicToAtomic casts on pointer values
// must not crash in emitPointerWithAlignment.

void atomic_store_via_atomic_ptr(int *_Atomic p, int val) {
  __atomic_store_n(p, val, __ATOMIC_SEQ_CST);
}

// LLVM: define {{.*}} void @atomic_store_via_atomic_ptr(ptr %{{.*}}, i32 noundef %{{.*}})
// LLVM:   load atomic ptr, ptr %{{.*}} seq_cst, align 8
// LLVM:   store atomic i32 %{{.*}}, ptr %{{.*}} seq_cst, align 4

// OGCG: define {{.*}} void @atomic_store_via_atomic_ptr(ptr %{{.*}}, i32 noundef %{{.*}})
// OGCG:   load atomic ptr, ptr %{{.*}} seq_cst, align 8
// OGCG:   store atomic i32 %{{.*}}, ptr %{{.*}} seq_cst, align 4

int atomic_load_via_atomic_ptr(int *_Atomic p) {
  return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}

// LLVM: define {{.*}} i32 @atomic_load_via_atomic_ptr(ptr %{{.*}})
// LLVM:   load atomic ptr, ptr %{{.*}} seq_cst, align 8
// LLVM:   load atomic i32, ptr %{{.*}} seq_cst, align 4

// OGCG: define {{.*}} i32 @atomic_load_via_atomic_ptr(ptr %{{.*}})
// OGCG:   load atomic ptr, ptr %{{.*}} seq_cst, align 8
// OGCG:   load atomic i32, ptr %{{.*}} seq_cst, align 4

// Dereferencing an _Atomic pointer (read through atomic-qualified pointer).
int deref_atomic_ptr(int *_Atomic p) {
  return *p;
}

// LLVM: define {{.*}} i32 @deref_atomic_ptr(ptr %{{.*}})
// LLVM:   load atomic ptr, ptr %{{.*}} seq_cst, align 8
// LLVM:   load i32, ptr %{{.*}}, align 4

// OGCG: define {{.*}} i32 @deref_atomic_ptr(ptr %{{.*}})
// OGCG:   load atomic ptr, ptr %{{.*}} seq_cst, align 8
// OGCG:   load i32, ptr %{{.*}}, align 4

// Writing through an _Atomic pointer.
void write_atomic_ptr(int *_Atomic p, int val) {
  *p = val;
}

// LLVM: define {{.*}} void @write_atomic_ptr(ptr %{{.*}}, i32 noundef %{{.*}})
// LLVM:   load atomic ptr, ptr %{{.*}} seq_cst, align 8
// LLVM:   store i32 %{{.*}}, ptr %{{.*}}, align 4

// OGCG: define {{.*}} void @write_atomic_ptr(ptr %{{.*}}, i32 noundef %{{.*}})
// OGCG:   load atomic ptr, ptr %{{.*}} seq_cst, align 8
// OGCG:   store i32 %{{.*}}, ptr %{{.*}}, align 4
