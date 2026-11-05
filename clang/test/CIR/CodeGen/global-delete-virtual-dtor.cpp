// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=CIR %s < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Global::delete on a type with a virtual destructor should use the
// offset-to-top from vtable entry -2 to derive the complete-object pointer,
// then call the complete destructor followed by the global operator delete.

struct X {
  virtual ~X();
  void operator delete(void*);
};

void f(X *xp) { ::delete xp; }

// Both CIR and OGCG should:
// 1. Load the vtable pointer
// 2. GEP to entry -2, load the offset-to-top
// 3. Apply the offset to get the complete-object pointer
// 4. Call the complete destructor (virtual, via vtable[0])
// 5. Call global ::operator delete with the complete pointer

// CIR-LABEL: define {{.*}} @_Z1fP1X(
// CIR:         icmp ne ptr %{{.*}}, null
// CIR:         %[[VTABLE:.*]] = load ptr, ptr %{{.*}}, align 8
// CIR:         %[[OFFSET_PTR:.*]] = getelementptr inbounds i64, ptr %[[VTABLE]], i64 -2
// CIR:         %[[OFFSET:.*]] = load i64, ptr %[[OFFSET_PTR]], align 8
// CIR:         %[[COMPLETE:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %[[OFFSET]]
// CIR:         call void %{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}})
// CIR:         call void @_ZdlPvm(ptr noundef %[[COMPLETE]], i64 noundef 8)

// OGCG-LABEL: define {{.*}} @_Z1fP1X(
// OGCG:         icmp eq ptr %{{.*}}, null
// OGCG:         %[[VTABLE:.*]] = load ptr, ptr %{{.*}}, align 8
// OGCG:         %[[OFFSET_PTR:.*]] = getelementptr inbounds i64, ptr %[[VTABLE]], i64 -2
// OGCG:         %[[OFFSET:.*]] = load i64, ptr %[[OFFSET_PTR]], align 8
// OGCG:         %[[COMPLETE:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %[[OFFSET]]
// OGCG:         call void %{{.*}}(ptr {{.*}} %{{.*}})
// OGCG:         call void @_ZdlPvm(ptr noundef %[[COMPLETE]], i64 noundef 8)
