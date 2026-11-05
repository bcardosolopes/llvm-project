// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -mconstructor-aliases -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -mconstructor-aliases -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -mconstructor-aliases -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -Oz -fclangir -mconstructor-aliases -emit-mlir=cir-abi %s -o %t.oz.cir
// RUN: FileCheck --check-prefix=OZ-CIR --input-file=%t.oz.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -Oz -fclangir -mconstructor-aliases -emit-llvm %s -o %t.oz-cir.ll
// RUN: FileCheck --check-prefix=OZ-LLVM --input-file=%t.oz-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -Oz -mconstructor-aliases -emit-llvm %s -o %t.oz.ll
// RUN: FileCheck --check-prefix=OZ-OGCG --input-file=%t.oz.ll %s

typedef __typeof(sizeof(int)) size_t;

struct SizedDelete {
  void operator delete(void*, size_t);
  int member;
};
void test_sized_delete(SizedDelete *x) {
  delete x;
}

void test_sized_delete_oz(SizedDelete *x) {
  delete x;
}

// CIR: cir.func {{.*}} @_Z17test_sized_deleteP11SizedDelete
// CIR:   %[[X:.*]] = cir.load{{.*}} %{{.*}}
// CIR:   %[[NULL:.*]] = cir.const #cir.ptr<null>
// CIR:   %[[IS_NOT_NULL:.*]] = cir.cmp(ne, %[[X]], %[[NULL]])
// CIR:   cir.if %[[IS_NOT_NULL]] {
// CIR:     %[[X_CAST:.*]] = cir.cast bitcast %[[X]] : !cir.ptr<!rec_SizedDelete> -> !cir.ptr<!void>
// CIR:     %[[OBJ_SIZE:.*]] = cir.const #cir.int<4> : !u64i
// CIR:     cir.call @_ZN11SizedDeletedlEPvm(%[[X_CAST]], %[[OBJ_SIZE]]) nothrow : (!cir.ptr<!void>, !u64i) -> ()
// CIR:   }

// LLVM: define dso_local void @_Z17test_sized_deleteP11SizedDelete
// LLVM:   %[[X:.*]] = load ptr, ptr %{{.*}}
// LLVM:   %[[ISNULL:.*]] = icmp ne ptr %[[X]], null
// LLVM:   br i1 %[[ISNULL]], label %[[DELETE_NOTNULL:.*]], label %[[DELETE_END:.*]]
// LLVM: [[DELETE_NOTNULL]]:
// LLVM:   call void @_ZN11SizedDeletedlEPvm(ptr noundef %[[X]], i64 noundef 4)

// OGCG: define dso_local void @_Z17test_sized_deleteP11SizedDelete
// OGCG:   %[[X:.*]] = load ptr, ptr %{{.*}}
// OGCG:   %[[ISNULL:.*]] = icmp eq ptr %[[X]], null
// OGCG:   br i1 %[[ISNULL]], label %{{.*}}, label %[[DELETE_NOTNULL:.*]]
// OGCG: [[DELETE_NOTNULL]]:
// OGCG:   call void @_ZN11SizedDeletedlEPvm(ptr noundef %[[X]], i64 noundef 4)

// OZ-CIR-LABEL: cir.func {{.*}} @_Z20test_sized_delete_ozP11SizedDelete
// OZ-CIR:   %[[X:.*]] = cir.load{{.*}} %{{.*}}
// OZ-CIR:   %[[NULL:.*]] = cir.const #cir.ptr<null>
// OZ-CIR:   %[[IS_NOT_NULL:.*]] = cir.cmp(ne, %[[X]], %[[NULL]])
// OZ-CIR:   cir.if %[[IS_NOT_NULL]] {
// OZ-CIR:     %[[X_CAST:.*]] = cir.cast bitcast %[[X]] : !cir.ptr<!rec_SizedDelete> -> !cir.ptr<!void>
// OZ-CIR:     %[[OBJ_SIZE:.*]] = cir.const #cir.int<4> : !u64i
// OZ-CIR:     cir.call @_ZN11SizedDeletedlEPvm(%[[X_CAST]], %[[OBJ_SIZE]]) nothrow : (!cir.ptr<!void>, !u64i) -> ()
// OZ-CIR:   }

// OZ-LLVM-LABEL: define dso_local void @_Z20test_sized_delete_ozP11SizedDelete
// OZ-LLVM:   icmp eq ptr %{{.*}}, null
// OZ-LLVM:   br i1 %{{.*}}, label %{{.*}}, label %[[DELETE_NOTNULL:.*]]
// OZ-LLVM: [[DELETE_NOTNULL]]:
// OZ-LLVM:   call void @_ZN11SizedDeletedlEPvm(ptr noundef {{.*}}%{{.*}}, i64 noundef 4)

// OZ-OGCG-LABEL: define dso_local void @_Z20test_sized_delete_ozP11SizedDelete
// OZ-OGCG-NOT: br i1
// OZ-OGCG:   call void @_ZN11SizedDeletedlEPvm(ptr noundef %{{.*}}, i64 noundef 4)

// This function is declared below the call in OGCG.
// OGCG: declare void @_ZN11SizedDeletedlEPvm(ptr noundef, i64 noundef)

struct Contents {
  ~Contents() {}
};
struct Container {
  Contents *contents;
  ~Container();
};
Container::~Container() { delete contents; }

// Container::~Container()
// CIR: cir.func {{.*}} @_ZN9ContainerD2Ev
// CIR:   %[[THIS:.*]] = cir.load %{{.*}}
// CIR:   %[[CONTENTS_PTR_ADDR:.*]] = cir.get_member %[[THIS]][0] {name = "contents"} : !cir.ptr<!rec_Container> -> !cir.ptr<!cir.ptr<!rec_Contents>>
// CIR:   %[[CONTENTS_PTR:.*]] = cir.load{{.*}} %[[CONTENTS_PTR_ADDR]]
// CIR:   %[[NULL:.*]] = cir.const #cir.ptr<null>
// CIR:   %[[IS_NOT_NULL:.*]] = cir.cmp(ne, %[[CONTENTS_PTR]], %[[NULL]])
// CIR:   cir.if %[[IS_NOT_NULL]] {
// Contents has no virtual bases, so its complete-object destructor is folded
// into the base one and the delete targets D2 directly, as in OGCG.
// CIR:     cir.call @_ZN8ContentsD2Ev(%[[CONTENTS_PTR]]) nothrow : (!cir.ptr<!rec_Contents>) -> ()
// CIR:     %[[CONTENTS_CAST:.*]] = cir.cast bitcast %[[CONTENTS_PTR]] : !cir.ptr<!rec_Contents> -> !cir.ptr<!void>
// CIR:     %[[OBJ_SIZE:.*]] = cir.const #cir.int<1> : !u64i
// CIR:     cir.call @_ZdlPvm(%[[CONTENTS_CAST]], %[[OBJ_SIZE]]) nothrow : (!cir.ptr<!void>, !u64i) -> ()
// CIR:   }

// operator delete(void*, unsigned long)
// CIR: cir.func {{.*}} @_ZdlPvm(!cir.ptr<!void>{{.*}}, !u64i{{.*}})

// LLVM: define dso_local void @_ZN9ContainerD2Ev
// LLVM:   %[[THIS:.*]] = load ptr, ptr %{{.*}}
// LLVM:   %[[CONTENTS_PTR_ADDR:.*]] = getelementptr inbounds nuw %struct.Container, ptr %[[THIS]], i32 0, i32 0
// LLVM:   %[[CONTENTS_PTR:.*]] = load ptr, ptr %[[CONTENTS_PTR_ADDR]]
// LLVM:   %[[ISNULL:.*]] = icmp ne ptr %[[CONTENTS_PTR]], null
// LLVM:   br i1 %[[ISNULL]], label %[[DELETE_NOTNULL:.*]], label %[[DELETE_END:.*]]
// LLVM: [[DELETE_NOTNULL]]:
// LLVM:   call void @_ZN8ContentsD2Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[CONTENTS_PTR]])
// LLVM:   call void @_ZdlPvm(ptr noundef %[[CONTENTS_PTR]], i64 noundef 1)

// OGCG: define dso_local void @_ZN9ContainerD2Ev
// OGCG:   %[[THIS:.*]] = load ptr, ptr %{{.*}}
// OGCG:   %[[CONTENTS:.*]] = getelementptr inbounds nuw %struct.Container, ptr %[[THIS]], i32 0, i32 0
// OGCG:   %[[CONTENTS_PTR:.*]] = load ptr, ptr %[[CONTENTS]]
// OGCG:   %[[ISNULL:.*]] = icmp eq ptr %[[CONTENTS_PTR]], null
// OGCG:   br i1 %[[ISNULL]], label %{{.*}}, label %[[DELETE_NOTNULL:.*]]
// OGCG: [[DELETE_NOTNULL]]:
// OGCG:   call void @_ZN8ContentsD2Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[CONTENTS_PTR]])
// OGCG:   call void @_ZdlPvm(ptr noundef %[[CONTENTS_PTR]], i64 noundef 1)

// These functions are declared/defined below the calls in OGCG.
// OGCG: define linkonce_odr void @_ZN8ContentsD2Ev
// OGCG: declare void @_ZdlPvm(ptr noundef, i64 noundef)

struct StructWithVirtualDestructor {
  virtual ~StructWithVirtualDestructor();
};

void destroy(StructWithVirtualDestructor *x) {
  delete x;
}

// CIR: cir.func {{.*}} @_Z7destroyP27StructWithVirtualDestructor(%[[X_ARG:.*]]: !cir.ptr<!rec_StructWithVirtualDestructor> {{.*}})
// CIR:   %[[X_ADDR:.*]] = cir.alloca !cir.ptr<!rec_StructWithVirtualDestructor>
// CIR:   cir.store %[[X_ARG]], %[[X_ADDR]]
// CIR:   %[[X:.*]] = cir.load{{.*}} %[[X_ADDR]]
// CIR:   %[[NULL:.*]] = cir.const #cir.ptr<null>
// CIR:   %[[IS_NOT_NULL:.*]] = cir.cmp(ne, %[[X]], %[[NULL]])
// CIR:   cir.if %[[IS_NOT_NULL]] {
// CIR:     %[[VTABLE_PTR:.*]] = cir.vtable.get_vptr %[[X]] : !cir.ptr<!rec_StructWithVirtualDestructor> -> !cir.ptr<!cir.vptr>
// CIR:     %[[VTABLE:.*]] = cir.load{{.*}} %[[VTABLE_PTR]] : !cir.ptr<!cir.vptr>, !cir.vptr
// CIR:     %[[DTOR_FN_ADDR_PTR:.*]] = cir.vtable.get_virtual_fn_addr %[[VTABLE]][1]
// CIR:     %[[DTOR_FN_ADDR:.*]] = cir.load{{.*}} %[[DTOR_FN_ADDR_PTR]]
// CIR:     cir.call %[[DTOR_FN_ADDR]](%[[X]]) nothrow {cir.call_arg_attrs = [{cir.align = 8 : i64, cir.dereferenceable = 8 : i64, cir.nonnull, cir.noundef}]}
// CIR:   }

// LLVM: define {{.*}} void @_Z7destroyP27StructWithVirtualDestructor(ptr noundef %[[X_ARG:.*]])
// LLVM:   %[[X_ADDR:.*]] = alloca ptr
// LLVM:   store ptr %[[X_ARG]], ptr %[[X_ADDR]]
// LLVM:   %[[X:.*]] = load ptr, ptr %[[X_ADDR]]
// LLVM:   %[[ISNULL:.*]] = icmp ne ptr %[[X]], null
// LLVM:   br i1 %[[ISNULL]], label %[[DELETE_NOTNULL:.*]], label %[[DELETE_END:.*]]
// LLVM: [[DELETE_NOTNULL]]:
// LLVM:   %[[VTABLE:.*]] = load ptr, ptr %[[X]]
// LLVM:   %[[DTOR_FN_ADDR_PTR:.*]] = getelementptr inbounds ptr, ptr %[[VTABLE]], i64 1
// LLVM:   %[[DTOR_FN_ADDR:.*]] = load ptr, ptr %[[DTOR_FN_ADDR_PTR]]
// LLVM:   call void %[[DTOR_FN_ADDR]](ptr noundef nonnull align 8 dereferenceable(8) %[[X]])

// OGCG: define {{.*}} void @_Z7destroyP27StructWithVirtualDestructor(ptr {{.*}} %[[X_ARG:.*]])
// OGCG:   %[[X_ADDR:.*]] = alloca ptr
// OGCG:   store ptr %[[X_ARG]], ptr %[[X_ADDR]]
// OGCG:   %[[X:.*]] = load ptr, ptr %[[X_ADDR]]
// OGCG:   %[[ISNULL:.*]] = icmp eq ptr %[[X]], null
// OGCG:   br i1 %[[ISNULL]], label %{{.*}}, label %[[DELETE_NOTNULL:.*]]
// OGCG: [[DELETE_NOTNULL]]:
// OGCG:   %[[VTABLE:.*]] = load ptr, ptr %[[X]]
// OGCG:   %[[DTOR_FN_ADDR_PTR:.*]] = getelementptr inbounds ptr, ptr %[[VTABLE]], i64 1
// OGCG:   %[[DTOR_FN_ADDR:.*]] = load ptr, ptr %[[DTOR_FN_ADDR_PTR]]
// OGCG:   call void %[[DTOR_FN_ADDR]](ptr {{.*}} %[[X]])

// Contents::~Contents() is emitted after the functions that use it.
// CIR: cir.func {{.*}} linkonce_odr @_ZN8ContentsD2Ev
// LLVM: define linkonce_odr void @_ZN8ContentsD2Ev
