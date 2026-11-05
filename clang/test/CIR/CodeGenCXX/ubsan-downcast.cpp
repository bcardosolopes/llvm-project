// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment,null,vptr,object-size \
// RUN:   -fsanitize-recover=alignment,null,vptr,object-size -o - %s \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment,null,vptr,object-size \
// RUN:   -fsanitize-recover=alignment,null,vptr,object-size -o - %s \
// RUN:   | grep -Fc "c\"'T'\\00\"" | FileCheck %s --check-prefix=T-DESC-COUNT
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -emit-llvm \
// RUN:   -fsanitize=alignment,null,vptr,object-size \
// RUN:   -fsanitize-recover=alignment,null,vptr,object-size -o - %s \
// RUN:   | grep -Fc "c\"'T'\\00\"" | FileCheck %s --check-prefix=T-DESC-COUNT

struct S {
  virtual ~S();
  int a, b;
};

struct T : S {
  int c;
};

S::~S() {}

class A16 {
  int a1, a2, a3;
};

class B16 {
  long b1, b2;
};

class C16 : public A16, public B16 {
  alignas(16) int c1;
};

// CHECK-LABEL: define {{.*}} @_Z12downcast_ptrP1S
void downcast_ptr(S *p) {
  // Null is allowed for pointer downcasts, so the remaining checks are guarded
  // behind a non-null branch.
  // CHECK: %[[NONNULL:.*]] = icmp ne ptr %{{.*}}, null, !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK: br i1 %[[NONNULL]], label %{{.*}}, label %{{.*}}, !nosanitize ![[NOSAN]]

  // CHECK: %[[SIZE:.*]] = call i64 @llvm.objectsize.i64.p0(ptr %{{.*}}, i1 false, i1 false, i1 false)
  // CHECK: %[[BIGENOUGH:.*]] = icmp uge i64 %[[SIZE]], 20, !nosanitize ![[NOSAN]]
  // CHECK: %[[PTRINT1:.*]] = ptrtoint ptr %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[MASK:.*]] = and i64 %[[PTRINT1]], 7, !nosanitize ![[NOSAN]]
  // CHECK: %[[ALIGNED:.*]] = icmp eq i64 %[[MASK]], 0, !nosanitize ![[NOSAN]]
  // CHECK: %[[OK:.*]] = and i1 %[[BIGENOUGH]], %[[ALIGNED]], !nosanitize ![[NOSAN]]
  // CHECK: %[[PTRINT2:.*]] = ptrtoint ptr %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[OK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_type_mismatch_v1(ptr {{.*}}, i64 %[[PTRINT2]]) #[[TYPECALL:[0-9]+]], !nosanitize ![[NOSAN]]
  // CHECK: %[[VPTR_PTRINT:.*]] = ptrtoint ptr %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[VPTR_CAST:.*]] = inttoptr i64 %[[VPTR_PTRINT]] to ptr, !nosanitize ![[NOSAN]]
  // CHECK: %[[VTABLE:.*]] = load ptr, ptr %[[VPTR_CAST]], align 8, !nosanitize ![[NOSAN]]
  // CHECK: %[[VTABLE_INT:.*]] = ptrtoint ptr %[[VTABLE]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[HASH_MUL:.*]] = mul i64 %[[VTABLE_INT]], -4658895280553007687, !nosanitize ![[NOSAN]]
  // CHECK: %[[HASH_SHIFT:.*]] = lshr i64 %[[HASH_MUL]], 31, !nosanitize ![[NOSAN]]
  // CHECK: %[[HASH_XOR:.*]] = xor i64 %[[HASH_MUL]], %[[HASH_SHIFT]], !nosanitize ![[NOSAN]]
  // CHECK: %[[HASH:.*]] = xor i64 7267112328930554169, %[[HASH_XOR]], !nosanitize ![[NOSAN]]
  // CHECK: %[[SLOT:.*]] = and i64 %[[HASH]], 127, !nosanitize ![[NOSAN]]
  // CHECK: %[[CACHE_PTR:.*]] = getelementptr inbounds [128 x i64], ptr @__ubsan_vptr_type_cache, i32 0, i64 %[[SLOT]], !nosanitize ![[NOSAN]]
  // CHECK: %[[CACHE_VAL:.*]] = load i64, ptr %[[CACHE_PTR]], align 8, !nosanitize ![[NOSAN]]
  // CHECK: %[[CACHE_HIT:.*]] = icmp eq i64 %[[CACHE_VAL]], %[[HASH]], !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[CACHE_HIT]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_dynamic_type_cache_miss(ptr {{.*}}, i64 %[[VPTR_PTRINT]], i64 %[[HASH]]) #[[VPTRCALL:[0-9]+]], !nosanitize ![[NOSAN]]
  (void)static_cast<T *>(p);
}

// CHECK-LABEL: define {{.*}} @_Z12downcast_refR1S
void downcast_ref(S &p) {
  // Reference downcasts run the full type/objectsize/alignment check directly.
  // CHECK: %[[NONNULL2:.*]] = icmp ne ptr %{{.*}}, null, !nosanitize ![[NOSAN]]
  // CHECK: %[[SIZE2:.*]] = call i64 @llvm.objectsize.i64.p0(ptr %{{.*}}, i1 false, i1 false, i1 false)
  // CHECK: %[[BIGENOUGH2:.*]] = icmp uge i64 %[[SIZE2]], 20, !nosanitize ![[NOSAN]]
  // CHECK: %[[PTRINT3:.*]] = ptrtoint ptr %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[MASK2:.*]] = and i64 %[[PTRINT3]], 7, !nosanitize ![[NOSAN]]
  // CHECK: %[[ALIGNED2:.*]] = icmp eq i64 %[[MASK2]], 0, !nosanitize ![[NOSAN]]
  // CHECK: %[[TMP:.*]] = and i1 %[[NONNULL2]], %[[BIGENOUGH2]], !nosanitize ![[NOSAN]]
  // CHECK: %[[OK2:.*]] = and i1 %[[TMP]], %[[ALIGNED2]], !nosanitize ![[NOSAN]]
  // CHECK: %[[PTRINT4:.*]] = ptrtoint ptr %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[OK2]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_type_mismatch_v1(ptr {{.*}}, i64 %[[PTRINT4]]) #[[TYPECALL]], !nosanitize ![[NOSAN]]
  // CHECK: %[[VPTR_PTRINT2:.*]] = ptrtoint ptr %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[VPTR_CAST2:.*]] = inttoptr i64 %[[VPTR_PTRINT2]] to ptr, !nosanitize ![[NOSAN]]
  // CHECK: %[[VTABLE2:.*]] = load ptr, ptr %[[VPTR_CAST2]], align 8, !nosanitize ![[NOSAN]]
  // CHECK: %[[VTABLE_INT2:.*]] = ptrtoint ptr %[[VTABLE2]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[HASH_MUL2:.*]] = mul i64 %[[VTABLE_INT2]], -4658895280553007687, !nosanitize ![[NOSAN]]
  // CHECK: %[[HASH_SHIFT2:.*]] = lshr i64 %[[HASH_MUL2]], 31, !nosanitize ![[NOSAN]]
  // CHECK: %[[HASH_XOR2:.*]] = xor i64 %[[HASH_MUL2]], %[[HASH_SHIFT2]], !nosanitize ![[NOSAN]]
  // CHECK: %[[HASH2:.*]] = xor i64 7267112328930554169, %[[HASH_XOR2]], !nosanitize ![[NOSAN]]
  // CHECK: %[[SLOT2:.*]] = and i64 %[[HASH2]], 127, !nosanitize ![[NOSAN]]
  // CHECK: %[[CACHE_PTR2:.*]] = getelementptr inbounds [128 x i64], ptr @__ubsan_vptr_type_cache, i32 0, i64 %[[SLOT2]], !nosanitize ![[NOSAN]]
  // CHECK: %[[CACHE_VAL2:.*]] = load i64, ptr %[[CACHE_PTR2]], align 8, !nosanitize ![[NOSAN]]
  // CHECK: %[[CACHE_HIT2:.*]] = icmp eq i64 %[[CACHE_VAL2]], %[[HASH2]], !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[CACHE_HIT2]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_dynamic_type_cache_miss(ptr {{.*}}, i64 %[[VPTR_PTRINT2]], i64 %[[HASH2]]) #[[VPTRCALL]], !nosanitize ![[NOSAN]]
  (void)static_cast<T &>(p);
}

// CHECK-LABEL: define {{.*}} @_Z18downcast_ptr_alignP3B16
void downcast_ptr_align(B16 *p) {
  // After subtracting the B16 base offset, CIR must use the derived C16
  // pointer alignment, not preserve the original B16 alignment.
  // CHECK: %[[SUB:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 -16
  // CHECK: %[[CAST:.*]] = select i1 %{{.*}}, ptr %{{.*}}, ptr %[[SUB]]
  // CHECK: %[[PTRINT1:.*]] = ptrtoint ptr %[[CAST]] to i64, !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK-NEXT: %[[MASK:.*]] = and i64 %[[PTRINT1]], 15, !nosanitize ![[NOSAN]]
  // CHECK-NEXT: %[[ALIGNED:.*]] = icmp eq i64 %[[MASK]], 0, !nosanitize ![[NOSAN]]
  (void)static_cast<C16 *>(p);
}

// CHECK-LABEL: define {{.*}} @_Z18downcast_ref_alignR3B16
void downcast_ref_align(B16 &p) {
  // CHECK: %[[SUB:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 -16
  // CHECK: %[[PTRINT2:.*]] = ptrtoint ptr %[[SUB]] to i64, !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK-NEXT: %[[MASK2:.*]] = and i64 %[[PTRINT2]], 15, !nosanitize ![[NOSAN]]
  // CHECK-NEXT: %[[ALIGNED2:.*]] = icmp eq i64 %[[MASK2]], 0, !nosanitize ![[NOSAN]]
  (void)static_cast<C16 &>(p);
}

// CHECK: declare void @__ubsan_handle_type_mismatch_v1(ptr, i64) #[[TYPEDECL:[0-9]+]]
// CHECK: attributes #[[TYPEDECL]] = { uwtable }
// CHECK: attributes #[[TYPECALL]] = { nomerge nounwind }

// T-DESC-COUNT: 1
