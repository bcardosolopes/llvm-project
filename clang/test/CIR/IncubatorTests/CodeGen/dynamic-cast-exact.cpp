// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O1 -fclangir -clangir-disable-passes -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O1 -fclangir -emit-llvm -fno-clangir-call-conv-lowering -o %t-cir.ll %s
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O1 -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

struct Base1 {
  virtual ~Base1();
};

struct Base2 {
  virtual ~Base2();
};

struct Derived final : Base1 {};

Derived *ptr_cast(Base1 *ptr) {
  return dynamic_cast<Derived *>(ptr);
  //      CHECK: %[[#SRC:]] = cir.load{{.*}} %{{.+}} : !cir.ptr<!cir.ptr<!rec_Base1>>, !cir.ptr<!rec_Base1>
  // CHECK-NEXT: %[[#NULL:]] = cir.const #cir.ptr<null> : !cir.ptr<!rec_Base1>
  // CHECK-NEXT: %[[#SRC_IS_NULL:]] = cir.cmp(eq, %[[#SRC]], %[[#NULL]]) : !cir.ptr<!rec_Base1>, !cir.bool
  // CHECK-NEXT: %[[#RESULT:]] = cir.ternary(%[[#SRC_IS_NULL]], true {
  // CHECK:        %{{.*}} = cir.const #cir.ptr<null> : !cir.ptr<!rec_Derived>
  // CHECK:        cir.yield %{{.*}} : !cir.ptr<!rec_Derived>
  // CHECK:      }, false {
  // CHECK:        %{{.*}} = cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 0, offset = 2>) : !cir.vptr
  // CHECK:        %{{.*}} = cir.cast bitcast %[[#SRC]] : !cir.ptr<!rec_Base1> -> !cir.ptr<!cir.vptr>
  // CHECK:        %{{.*}} = cir.load{{.*}} %{{.*}} : !cir.ptr<!cir.vptr>, !cir.vptr
  // CHECK:        %{{.*}} = cir.cmp(eq, %{{.*}}, %{{.*}}) : !cir.vptr, !cir.bool
  // CHECK:        %{{.*}} = cir.ternary(%{{.*}}, true {
  // CHECK:          %{{.*}} = cir.cast bitcast %[[#SRC]] : !cir.ptr<!rec_Base1> -> !cir.ptr<!rec_Derived>
  // CHECK:          cir.yield %{{.*}} : !cir.ptr<!rec_Derived>
  // CHECK:        }, false {
  // CHECK:          %{{.*}} = cir.const #cir.ptr<null> : !cir.ptr<!rec_Derived>
  // CHECK:          cir.yield %{{.*}} : !cir.ptr<!rec_Derived>
  // CHECK:        }) : (!cir.bool) -> !cir.ptr<!rec_Derived>
  // CHECK:        cir.yield %{{.*}} : !cir.ptr<!rec_Derived>
  // CHECK:      }) : (!cir.bool) -> !cir.ptr<!rec_Derived>
}

//      LLVM: define dso_local ptr @_Z8ptr_castP5Base1(ptr {{.*}} %[[#SRC:]])
// LLVM-NEXT:   %[[SRC_IS_NULL:.*]] = icmp eq ptr %[[#SRC]], null
// LLVM-NEXT:   br i1 %[[SRC_IS_NULL]], label %[[#LABEL_END:]], label %[[#LABEL_NONNULL:]]
//      LLVM: [[#LABEL_NONNULL]]:
// LLVM-NEXT:   %[[#VPTR:]] = load ptr, ptr %[[#SRC]], align 8
// LLVM-NEXT:   %[[#SUCCESS:]] = icmp eq ptr %[[#VPTR]], getelementptr inbounds nuw (i8, ptr @_ZTV7Derived, i64 16)
// LLVM-NEXT:   %[[EXACT_RESULT:.*]] = select i1 %[[#SUCCESS]], ptr %[[#SRC]], ptr null
// LLVM-NEXT:   br label %[[#LABEL_END]]
//      LLVM: [[#LABEL_END]]:
// LLVM-NEXT:   %[[#RESULT:]] = phi ptr [ %[[EXACT_RESULT]], %[[#LABEL_NONNULL]] ], [ null, %{{.*}} ]
// LLVM-NEXT:   ret ptr %[[#RESULT]]
// LLVM-NEXT: }

//      OGCG: define{{.*}} ptr @_Z8ptr_castP5Base1(ptr {{.*}} %[[SRC:.*]])
// OGCG-NEXT: entry:
// OGCG-NEXT:   %[[NULL_CHECK:.*]] = icmp eq ptr %[[SRC]], null
// OGCG-NEXT:   br i1 %[[NULL_CHECK]], label %[[LABEL_NULL:.*]], label %[[LABEL_NOTNULL:.*]]
//      OGCG: [[LABEL_NOTNULL]]:
// OGCG-NEXT:   %[[VTABLE:.*]] = load ptr, ptr %[[SRC]], align 8
// OGCG-NEXT:   %[[VTABLE_CHECK:.*]] = icmp eq ptr %[[VTABLE]], getelementptr inbounds {{.*}} (i8, ptr @_ZTV7Derived, i64 16)
// OGCG-NEXT:   br i1 %[[VTABLE_CHECK]], label %[[LABEL_END:.*]], label %[[LABEL_NULL]]
//      OGCG: [[LABEL_NULL]]:
// OGCG-NEXT:   br label %[[LABEL_END]]
//      OGCG: [[LABEL_END]]:
// OGCG-NEXT:   %[[RESULT:.*]] = phi ptr [ %[[SRC]], %[[LABEL_NOTNULL]] ], [ null, %[[LABEL_NULL]] ]
// OGCG-NEXT:   ret ptr %[[RESULT]]
// OGCG-NEXT: }

Derived &ref_cast(Base1 &ref) {
  return dynamic_cast<Derived &>(ref);
  //      CHECK: %[[#SRC:]] = cir.load{{.*}} %{{.+}} : !cir.ptr<!cir.ptr<!rec_Base1>>, !cir.ptr<!rec_Base1>
  // CHECK:      %{{.*}} = cir.vtable.address_point(@_ZTV7Derived, address_point = <index = 0, offset = 2>) : !cir.vptr
  // CHECK:      %{{.*}} = cir.cast bitcast %[[#SRC]] : !cir.ptr<!rec_Base1> -> !cir.ptr<!cir.vptr>
  // CHECK:      %{{.*}} = cir.load{{.*}} %{{.*}} : !cir.ptr<!cir.vptr>, !cir.vptr
  // CHECK:      %{{.*}} = cir.cmp(eq, %{{.*}}, %{{.*}}) : !cir.vptr, !cir.bool
  // CHECK:      %{{.*}} = cir.unary(not, %{{.*}}) : !cir.bool, !cir.bool
  // CHECK:      cir.if %{{.*}} {
  // CHECK:        cir.call @__cxa_bad_cast() : () -> ()
  // CHECK:        cir.unreachable
  // CHECK:      }
  // CHECK:      %{{.*}} = cir.cast bitcast %[[#SRC]] : !cir.ptr<!rec_Base1> -> !cir.ptr<!rec_Derived>
}

//      LLVM: define dso_local noundef ptr @_Z8ref_castR5Base1(ptr readonly returned captures(ret: address, provenance) %[[#SRC:]])
// LLVM-NEXT:   %[[#VPTR:]] = load ptr, ptr %[[#SRC]], align 8
// LLVM-NEXT:   %[[OK:.+]] = icmp eq ptr %[[#VPTR]], getelementptr inbounds nuw (i8, ptr @_ZTV7Derived, i64 16)
// LLVM-NEXT:   br i1 %[[OK]], label %[[#LABEL_OK:]], label %[[#LABEL_FAIL:]]
//      LLVM: [[#LABEL_FAIL]]:
// LLVM-NEXT:   tail call void @__cxa_bad_cast()
// LLVM-NEXT:   unreachable
//      LLVM: [[#LABEL_OK]]:
// LLVM-NEXT:   ret ptr %[[#SRC]]
// LLVM-NEXT: }

//      OGCG: define{{.*}} ptr @_Z8ref_castR5Base1(ptr {{.*}} %[[REF:.*]])
// OGCG-NEXT: entry:
// OGCG-NEXT:   %[[VTABLE:.*]] = load ptr, ptr %[[REF]], align 8
// OGCG-NEXT:   %[[VTABLE_CHECK:.*]] = icmp eq ptr %[[VTABLE]], getelementptr inbounds {{.*}} (i8, ptr @_ZTV7Derived, i64 16)
// OGCG-NEXT:   br i1 %[[VTABLE_CHECK]], label %[[LABEL_END:.*]], label %[[LABEL_NULL:.*]]
//      OGCG: [[LABEL_NULL]]:
// OGCG-NEXT:   {{.*}}call void @__cxa_bad_cast()
// OGCG-NEXT:   unreachable
//      OGCG: [[LABEL_END]]:
// OGCG-NEXT:   ret ptr %[[REF]]
// OGCG-NEXT: }

Derived *ptr_cast_always_fail(Base2 *ptr) {
  return dynamic_cast<Derived *>(ptr);
  //      CHECK: %{{.+}} = cir.load{{.*}} %{{.+}} : !cir.ptr<!cir.ptr<!rec_Base2>>, !cir.ptr<!rec_Base2>
  // CHECK-NEXT: %[[#RESULT:]] = cir.const #cir.ptr<null> : !cir.ptr<!rec_Derived>
  // CHECK-NEXT: cir.store %[[#RESULT]], %{{.+}} : !cir.ptr<!rec_Derived>, !cir.ptr<!cir.ptr<!rec_Derived>>
}

//      LLVM: define dso_local noalias noundef ptr @_Z20ptr_cast_always_failP5Base2(ptr readnone captures(none) %{{.+}})
// LLVM-NEXT:   ret ptr null
// LLVM-NEXT: }

Derived &ref_cast_always_fail(Base2 &ref) {
  return dynamic_cast<Derived &>(ref);
  //      CHECK: %{{.+}} = cir.load{{.*}} %{{.+}} : !cir.ptr<!cir.ptr<!rec_Base2>>, !cir.ptr<!rec_Base2>
  // CHECK-NEXT: cir.call @__cxa_bad_cast() : () -> ()
  // CHECK-NEXT: cir.unreachable
}

//      LLVM: define dso_local noalias noundef nonnull ptr @_Z20ref_cast_always_failR5Base2(ptr readnone captures(none) %{{.+}})
// LLVM-NEXT:   tail call void @__cxa_bad_cast()
// LLVM-NEXT:   unreachable
// LLVM-NEXT: }
