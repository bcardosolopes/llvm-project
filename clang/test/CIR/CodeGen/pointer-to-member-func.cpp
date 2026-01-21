// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -mmlir -mlir-print-ir-before=cir-cxxabi-lowering %s -o %t.cir 2> %t-before.cir
// RUN: FileCheck --check-prefix=CIR-BEFORE --input-file=%t-before.cir %s
// RUN: FileCheck --check-prefix=CIR-AFTER --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

struct Foo {
  void m1(int);
  virtual void m2(int);
  virtual void m3(int);
};

auto make_non_virtual() -> void (Foo::*)(int) {
  return &Foo::m1;
}

// CIR-BEFORE: cir.func {{.*}} @_Z16make_non_virtualv() -> !cir.method<!cir.func<(!s32i)> in !rec_Foo>
// CIR-BEFORE:   %[[RETVAL:.*]] = cir.alloca !cir.method<!cir.func<(!s32i)> in !rec_Foo>, !cir.ptr<!cir.method<!cir.func<(!s32i)> in !rec_Foo>>, ["__retval"]
// CIR-BEFORE:   %[[METHOD_PTR:.*]] = cir.const #cir.method<@_ZN3Foo2m1Ei> : !cir.method<!cir.func<(!s32i)> in !rec_Foo>
// CIR-BEFORE:   cir.store %[[METHOD_PTR]], %[[RETVAL]]
// CIR-BEFORE:   %[[RET:.*]] = cir.load %[[RETVAL]]
// CIR-BEFORE:   cir.return %[[RET]] : !cir.method<!cir.func<(!s32i)> in !rec_Foo>

// CIR-AFTER: cir.func {{.*}} @_Z16make_non_virtualv() -> !rec_anon_struct {{.*}}{
// CIR-AFTER:   %[[RETVAL:.*]] = cir.alloca !rec_anon_struct1, !cir.ptr<!rec_anon_struct1>, ["__retval"]
// CIR-AFTER:   %[[METHOD_PTR:.*]] = cir.const #cir.const_record<{#cir.global_view<@_ZN3Foo2m1Ei> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct1
// CIR-AFTER:   cir.store %[[METHOD_PTR]], %[[RETVAL]]
// CIR-AFTER:   %{{.*}} = cir.load %[[RETVAL]]
// CIR-AFTER:   %[[RETVAL_CAST:.*]] = cir.cast bitcast %[[RETVAL]] : !cir.ptr<!rec_anon_struct1> -> !cir.ptr<!rec_anon_struct>
// CIR-AFTER:   %[[RET:.*]] = cir.load %[[RETVAL_CAST]]
// CIR-AFTER:   cir.return %[[RET]] : !rec_anon_struct

// LLVM: define {{.*}} { i64, i64 } @_Z16make_non_virtualv()
// LLVM:   %[[RETVAL:.*]] = alloca { i64, i64 }
// LLVM:   store { i64, i64 } { i64 ptrtoint (ptr @_ZN3Foo2m1Ei to i64), i64 0 }, ptr %[[RETVAL]]
// LLVM:   %{{.*}} = load { i64, i64 }, ptr %[[RETVAL]]
// LLVM:   %[[RET:.*]] = load { i64, i64 }, ptr %[[RETVAL]]
// LLVM:   ret { i64, i64 } %[[RET]]

// OGCG: define {{.*}} { i64, i64 } @_Z16make_non_virtualv()
// OGCG:   ret { i64, i64 } { i64 ptrtoint (ptr @_ZN3Foo2m1Ei to i64), i64 0 }

auto make_virtual() -> void (Foo::*)(int) {
  return &Foo::m3;
}

// CIR-BEFORE: cir.func {{.*}} @_Z12make_virtualv() -> !cir.method<!cir.func<(!s32i)> in !rec_Foo>
// CIR-BEFORE:   %[[RETVAL:.*]] = cir.alloca !cir.method<!cir.func<(!s32i)> in !rec_Foo>, !cir.ptr<!cir.method<!cir.func<(!s32i)> in !rec_Foo>>, ["__retval"]
// CIR-BEFORE:   %[[METHOD_PTR:.*]] = cir.const #cir.method<vtable_offset = 8> : !cir.method<!cir.func<(!s32i)> in !rec_Foo>
// CIR-BEFORE:   cir.store %[[METHOD_PTR]], %[[RETVAL]]
// CIR-BEFORE:   %[[RET:.*]] = cir.load %[[RETVAL]]
// CIR-BEFORE:   cir.return %[[RET]] : !cir.method<!cir.func<(!s32i)> in !rec_Foo>

// CIR-AFTER: cir.func {{.*}} @_Z12make_virtualv() -> !rec_anon_struct {{.*}}{
// CIR-AFTER:   %[[RETVAL:.*]] = cir.alloca !rec_anon_struct1, !cir.ptr<!rec_anon_struct1>, ["__retval"]
// CIR-AFTER:   %[[METHOD_PTR:.*]] = cir.const #cir.const_record<{#cir.int<9> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct1
// CIR-AFTER:   cir.store %[[METHOD_PTR]], %[[RETVAL]]
// CIR-AFTER:   %{{.*}} = cir.load %[[RETVAL]]
// CIR-AFTER:   %[[RETVAL_CAST:.*]] = cir.cast bitcast %[[RETVAL]] : !cir.ptr<!rec_anon_struct1> -> !cir.ptr<!rec_anon_struct>
// CIR-AFTER:   %[[RET:.*]] = cir.load %[[RETVAL_CAST]]
// CIR-AFTER:   cir.return %[[RET]] : !rec_anon_struct

// LLVM: define {{.*}} { i64, i64 } @_Z12make_virtualv()
// LLVM:   store { i64, i64 } { i64 9, i64 0 }, ptr %{{.*}}

// OGCG: define {{.*}} { i64, i64 } @_Z12make_virtualv()
// OGCG:   ret { i64, i64 } { i64 9, i64 0 }

auto make_null() -> void (Foo::*)(int) {
  return nullptr;
}

// CIR-BEFORE: cir.func {{.*}} @_Z9make_nullv() -> !cir.method<!cir.func<(!s32i)> in !rec_Foo>
// CIR-BEFORE:   %[[RETVAL:.*]] = cir.alloca !cir.method<!cir.func<(!s32i)> in !rec_Foo>, !cir.ptr<!cir.method<!cir.func<(!s32i)> in !rec_Foo>>, ["__retval"]
// CIR-BEFORE:   %[[METHOD_PTR:.*]] = cir.const #cir.method<null> : !cir.method<!cir.func<(!s32i)> in !rec_Foo>
// CIR-BEFORE:   cir.store %[[METHOD_PTR]], %[[RETVAL]]
// CIR-BEFORE:   %[[RET:.*]] = cir.load %[[RETVAL]]
// CIR-BEFORE:   cir.return %[[RET]] : !cir.method<!cir.func<(!s32i)> in !rec_Foo>

// CIR-AFTER: cir.func {{.*}} @_Z9make_nullv() -> !rec_anon_struct {{.*}}{
// CIR-AFTER:   %[[RETVAL:.*]] = cir.alloca !rec_anon_struct1, !cir.ptr<!rec_anon_struct1>, ["__retval"]
// CIR-AFTER:   %[[METHOD_PTR:.*]] = cir.const #cir.const_record<{#cir.int<0> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct1
// CIR-AFTER:   cir.store %[[METHOD_PTR]], %[[RETVAL]]

// LLVM: define {{.*}} { i64, i64 } @_Z9make_nullv()
// LLVM:   store { i64, i64 } zeroinitializer, ptr %{{.*}}

// OGCG: define {{.*}} { i64, i64 } @_Z9make_nullv()
// OGCG:   ret { i64, i64 } zeroinitializer

void call(Foo *obj, void (Foo::*func)(int), int arg) {
  (obj->*func)(arg);
}

// CIR-BEFORE: cir.func {{.*}} @_Z4callP3FooMS_FviEi
// CIR-BEFORE:   %[[OBJ:.*]] = cir.load{{.*}} %{{.*}} : !cir.ptr<!cir.ptr<!rec_Foo>>, !cir.ptr<!rec_Foo>
// CIR-BEFORE:   %[[FUNC:.*]] = cir.load{{.*}} : !cir.ptr<!cir.method<!cir.func<(!s32i)> in !rec_Foo>>, !cir.method<!cir.func<(!s32i)> in !rec_Foo>
// CIR-BEFORE:   %[[CALLEE:.*]], %[[THIS:.*]] = cir.get_method %[[FUNC]], %[[OBJ]] : (!cir.method<!cir.func<(!s32i)> in !rec_Foo>, !cir.ptr<!rec_Foo>) -> (!cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>, !cir.ptr<!void>)
// CIR-BEFORE:   %[[ARG:.*]] = cir.load{{.*}} %{{.*}} : !cir.ptr<!s32i>, !s32i
// CIR-BEFORE:   cir.call %[[CALLEE]](%[[THIS]], %[[ARG]]) : (!cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>, !cir.ptr<!void>, !s32i) -> ()

// CIR-AFTER: cir.func {{.*}} @_Z4callP3FooMS_FviEi
// CIR-AFTER:   %[[OBJ:.*]] = cir.load{{.*}} %{{.*}} : !cir.ptr<!cir.ptr<!rec_Foo>>, !cir.ptr<!rec_Foo>
// CIR-AFTER:   %[[FUNC:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   %[[VIRT_BIT:.*]] = cir.const #cir.int<1> : !s64i
// CIR-AFTER:   %[[ADJ:.*]] = cir.extract_member %[[FUNC]][1] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %[[THIS:.*]] = cir.cast bitcast %[[OBJ]] : !cir.ptr<!rec_Foo> -> !cir.ptr<!void>
// CIR-AFTER:   %[[ADJUSTED_THIS:.*]] = cir.ptr_stride %[[THIS]], %[[ADJ]] : (!cir.ptr<!void>, !s64i) -> !cir.ptr<!void>
// CIR-AFTER:   %[[METHOD_PTR:.*]] = cir.extract_member %[[FUNC]][0] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %[[VIRT_BIT_TEST:.*]] = cir.binop(and, %[[METHOD_PTR]], %[[VIRT_BIT]]) : !s64i
// CIR-AFTER:   %[[IS_VIRTUAL:.*]] = cir.cmp(eq, %[[VIRT_BIT_TEST]], %[[VIRT_BIT]]) : !s64i, !cir.bool
// CIR-AFTER:   cir.brcond %[[IS_VIRTUAL]] ^bb1, ^bb2
// CIR-AFTER: ^bb1:
// CIR-AFTER:   %[[VTABLE_PTR:.*]] = cir.cast bitcast %[[OBJ]] : !cir.ptr<!rec_Foo> -> !cir.ptr<!cir.ptr<!s8i>>
// CIR-AFTER:   %[[VTABLE:.*]] = cir.load %[[VTABLE_PTR]] : !cir.ptr<!cir.ptr<!s8i>>, !cir.ptr<!s8i>
// CIR-AFTER:   %[[OFFSET:.*]] = cir.binop(sub, %[[METHOD_PTR]], %[[VIRT_BIT]]) : !s64i
// CIR-AFTER:   %[[VTABLE_SLOT:.*]] = cir.ptr_stride %[[VTABLE]], %[[OFFSET]] : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// CIR-AFTER:   %[[VIRTUAL_FN_PTR:.*]] = cir.cast bitcast %[[VTABLE_SLOT]] : !cir.ptr<!s8i> -> !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>>
// CIR-AFTER:   %[[VIRTUAL_FN_PTR_LOAD:.*]] = cir.load %[[VIRTUAL_FN_PTR]] : !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>>, !cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>
// CIR-AFTER:   cir.br ^bb3(%[[VIRTUAL_FN_PTR_LOAD]] : !cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>)
// CIR-AFTER: ^bb2:
// CIR-AFTER:   %[[CALLEE_PTR:.*]] = cir.cast int_to_ptr %[[METHOD_PTR]] : !s64i -> !cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>
// CIR-AFTER:   cir.br ^bb3(%[[CALLEE_PTR]] : !cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>)
// CIR-AFTER: ^bb3(%[[CALLEE:.*]]: !cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>
// CIR-AFTER:   %[[ARG:.*]] = cir.load{{.*}} %{{.*}} : !cir.ptr<!s32i>, !s32i
// CIR-AFTER:   cir.call %[[CALLEE]](%[[ADJUSTED_THIS]], %[[ARG]]) : (!cir.ptr<!cir.func<(!cir.ptr<!void>, !s32i)>>, !cir.ptr<!void>, !s32i) -> ()

// LLVM: define {{.*}} @_Z4callP3FooMS_FviEi(ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i32 %{{.*}})
// LLVM:   %[[OBJ:.*]] = load ptr, ptr %{{.*}}
// LLVM:   %[[MEMFN_PTR:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM:   %[[THIS_ADJ:.*]] = extractvalue { i64, i64 } %[[MEMFN_PTR]], 1
// LLVM:   %[[ADJUSTED_THIS:.*]] = getelementptr i8, ptr %[[OBJ]], i64 %[[THIS_ADJ]]
// LLVM:   %[[PTR_FIELD:.*]] = extractvalue { i64, i64 } %[[MEMFN_PTR]], 0
// LLVM:   %[[VIRT_BIT:.*]] = and i64 %[[PTR_FIELD]], 1
// LLVM:   %[[IS_VIRTUAL:.*]] = icmp eq i64 %[[VIRT_BIT]], 1
// LLVM:   br i1 %[[IS_VIRTUAL]], label %[[HANDLE_VIRTUAL:.*]], label %[[HANDLE_NON_VIRTUAL:.*]]
// LLVM: [[HANDLE_VIRTUAL]]:
// LLVM:   %[[VTABLE:.*]] = load ptr, ptr %[[OBJ]]
// LLVM:   %[[OFFSET:.*]] = sub i64 %[[PTR_FIELD]], 1
// LLVM:   %[[VTABLE_SLOT:.*]] = getelementptr i8, ptr %[[VTABLE]], i64 %[[OFFSET]]
// LLVM:   %[[VIRTUAL_FN_PTR:.*]] = load ptr, ptr %[[VTABLE_SLOT]]
// LLVM:   br label %[[CONTINUE:.*]]
// LLVM: [[HANDLE_NON_VIRTUAL]]:
// LLVM:   %[[FUNC_PTR:.*]] = inttoptr i64 %[[PTR_FIELD]] to ptr
// LLVM:   br label %[[CONTINUE]]
// LLVM: [[CONTINUE]]:
// LLVM:   %[[CALLEE_PTR:.*]] = phi ptr [ %[[FUNC_PTR]], %[[HANDLE_NON_VIRTUAL]] ], [ %[[VIRTUAL_FN_PTR]], %[[HANDLE_VIRTUAL]] ]
// LLVM:   %[[ARG:.*]] = load i32, ptr %{{.+}}
// LLVM:   call void %[[CALLEE_PTR]](ptr %[[ADJUSTED_THIS]], i32 %[[ARG]])
// LLVM: }

// OGCG: define {{.*}} @_Z4callP3FooMS_FviEi
// OGCG:   %[[OBJ:.*]] = load ptr, ptr %{{.*}}
// OGCG:   %[[MEMFN_PTR:.*]] = load { i64, i64 }, ptr %{{.*}}
// OGCG:   %[[THIS_ADJ:.*]] = extractvalue { i64, i64 } %[[MEMFN_PTR]], 1
// OGCG:   %[[ADJUSTED_THIS:.*]] = getelementptr inbounds i8, ptr %[[OBJ]], i64 %[[THIS_ADJ]]
// OGCG:   %[[PTR_FIELD:.*]] = extractvalue { i64, i64 } %[[MEMFN_PTR]], 0
// OGCG:   %[[VIRT_BIT:.*]] = and i64 %[[PTR_FIELD]], 1
// OGCG:   %[[IS_VIRTUAL:.*]] = icmp ne i64 %[[VIRT_BIT]], 0
// OGCG:   br i1 %[[IS_VIRTUAL]], label %[[HANDLE_VIRTUAL:.*]], label %[[HANDLE_NON_VIRTUAL:.*]]
// OGCG: [[HANDLE_VIRTUAL]]:
// OGCG:   %[[VTABLE:.*]] = load ptr, ptr %[[ADJUSTED_THIS]]
// OGCG:   %[[OFFSET:.*]] = sub i64 %[[PTR_FIELD]], 1
// OGCG:   %[[VTABLE_SLOT:.*]] = getelementptr i8, ptr %[[VTABLE]], i64 %[[OFFSET]]
// OGCG:   %[[VIRTUAL_FN_PTR:.*]] = load ptr, ptr %[[VTABLE_SLOT]]
// OGCG:   br label %[[CONTINUE:.*]]
// OGCG: [[HANDLE_NON_VIRTUAL]]:
// OGCG:   %[[FUNC_PTR:.*]] = inttoptr i64 %[[PTR_FIELD]] to ptr
// OGCG:   br label %[[CONTINUE]]
// OGCG: [[CONTINUE]]:
// OGCG:   %[[CALLEE_PTR:.*]] = phi ptr [ %[[VIRTUAL_FN_PTR]], %[[HANDLE_VIRTUAL]] ], [ %[[FUNC_PTR]], %[[HANDLE_NON_VIRTUAL]] ]
// OGCG:   %[[ARG:.*]] = load i32, ptr %{{.+}}
// OGCG:   call void %[[CALLEE_PTR]](ptr {{.*}} %[[ADJUSTED_THIS]], i32 {{.*}} %[[ARG]])
// OGCG: }

bool cmp_eq(void (Foo::*lhs)(int), void (Foo::*rhs)(int)) {
  return lhs == rhs;
}

// CIR-BEFORE-LABEL: @_Z6cmp_eqM3FooFviES1_
// CIR-BEFORE: %{{.+}} = cir.cmp(eq, %{{.+}}, %{{.+}}) : !cir.method<!cir.func<(!s32i)> in !rec_Foo>, !cir.bool

// CIR-AFTER-LABEL: @_Z6cmp_eqM3FooFviES1_
// CIR-AFTER:   %[[LHS:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   %[[RHS:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   %{{.*}} = cir.const #cir.int<0> : !s64i
// CIR-AFTER:   %[[LHS_PTR:.*]] = cir.extract_member %[[LHS]][0] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %[[RHS_PTR:.*]] = cir.extract_member %[[RHS]][0] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %{{.*}} = cir.cmp(eq, %[[LHS_PTR]], %[[RHS_PTR]]) : !s64i, !cir.bool
// CIR-AFTER:   %{{.*}} = cir.cmp(eq, %[[LHS_PTR]], %{{.*}}) : !s64i, !cir.bool
// CIR-AFTER:   %[[LHS_ADJ:.*]] = cir.extract_member %[[LHS]][1] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %[[RHS_ADJ:.*]] = cir.extract_member %[[RHS]][1] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %{{.*}} = cir.cmp(eq, %[[LHS_ADJ]], %[[RHS_ADJ]]) : !s64i, !cir.bool

// LLVM-LABEL: @_Z6cmp_eqM3FooFviES1_
//      LLVM: %[[LHS:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT: %[[RHS:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT: %[[LHS_PTR:.*]] = extractvalue { i64, i64 } %[[LHS]], 0
// LLVM-NEXT: %[[RHS_PTR:.*]] = extractvalue { i64, i64 } %[[RHS]], 0
// LLVM-NEXT: %[[PTR_CMP:.*]] = icmp eq i64 %[[LHS_PTR]], %[[RHS_PTR]]
// LLVM-NEXT: %[[PTR_NULL:.*]] = icmp eq i64 %[[LHS_PTR]], 0
// LLVM-NEXT: %[[LHS_ADJ:.*]] = extractvalue { i64, i64 } %[[LHS]], 1
// LLVM-NEXT: %[[RHS_ADJ:.*]] = extractvalue { i64, i64 } %[[RHS]], 1
// LLVM-NEXT: %[[ADJ_CMP:.*]] = icmp eq i64 %[[LHS_ADJ]], %[[RHS_ADJ]]
// LLVM-NEXT: %[[TMP:.*]] = or i1 %[[PTR_NULL]], %[[ADJ_CMP]]
// LLVM-NEXT: %{{.*}} = and i1 %[[TMP]], %[[PTR_CMP]]

// OGCG-LABEL: @_Z6cmp_eqM3FooFviES1_
//      OGCG: %[[LHS_PTR:.*]] = extractvalue { i64, i64 } %{{.*}}, 0
// OGCG-NEXT: %[[RHS_PTR:.*]] = extractvalue { i64, i64 } %{{.*}}, 0
// OGCG-NEXT: %[[PTR_CMP:.*]] = icmp eq i64 %[[LHS_PTR]], %[[RHS_PTR]]
// OGCG-NEXT: %[[PTR_NULL:.*]] = icmp eq i64 %[[LHS_PTR]], 0
// OGCG-NEXT: %[[LHS_ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG-NEXT: %[[RHS_ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG-NEXT: %[[ADJ_CMP:.*]] = icmp eq i64 %[[LHS_ADJ]], %[[RHS_ADJ]]
// OGCG-NEXT: %[[TMP:.*]] = or i1 %[[PTR_NULL]], %[[ADJ_CMP]]
// OGCG-NEXT: %{{.*}} = and i1 %[[PTR_CMP]], %[[TMP]]

bool cmp_ne(void (Foo::*lhs)(int), void (Foo::*rhs)(int)) {
  return lhs != rhs;
}

// CIR-BEFORE-LABEL: @_Z6cmp_neM3FooFviES1_
// CIR-BEFORE: %{{.+}} = cir.cmp(ne, %{{.+}}, %{{.+}}) : !cir.method<!cir.func<(!s32i)> in !rec_Foo>, !cir.bool

// CIR-AFTER-LABEL: @_Z6cmp_neM3FooFviES1_
// CIR-AFTER:   %[[LHS:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   %[[RHS:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   %{{.*}} = cir.const #cir.int<0> : !s64i
// CIR-AFTER:   %[[LHS_PTR:.*]] = cir.extract_member %[[LHS]][0] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %[[RHS_PTR:.*]] = cir.extract_member %[[RHS]][0] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %{{.*}} = cir.cmp(ne, %[[LHS_PTR]], %[[RHS_PTR]]) : !s64i, !cir.bool
// CIR-AFTER:   %{{.*}} = cir.cmp(ne, %[[LHS_PTR]], %{{.*}}) : !s64i, !cir.bool
// CIR-AFTER:   %[[LHS_ADJ:.*]] = cir.extract_member %[[LHS]][1] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %[[RHS_ADJ:.*]] = cir.extract_member %[[RHS]][1] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %{{.*}} = cir.cmp(ne, %[[LHS_ADJ]], %[[RHS_ADJ]]) : !s64i, !cir.bool

// LLVM-LABEL: @_Z6cmp_neM3FooFviES1_
//      LLVM: %[[LHS:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT: %[[RHS:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT: %[[LHS_PTR:.*]] = extractvalue { i64, i64 } %[[LHS]], 0
// LLVM-NEXT: %[[RHS_PTR:.*]] = extractvalue { i64, i64 } %[[RHS]], 0
// LLVM-NEXT: %[[PTR_CMP:.*]] = icmp ne i64 %[[LHS_PTR]], %[[RHS_PTR]]
// LLVM-NEXT: %[[PTR_NULL:.*]] = icmp ne i64 %[[LHS_PTR]], 0
// LLVM-NEXT: %[[LHS_ADJ:.*]] = extractvalue { i64, i64 } %[[LHS]], 1
// LLVM-NEXT: %[[RHS_ADJ:.*]] = extractvalue { i64, i64 } %[[RHS]], 1
// LLVM-NEXT: %[[ADJ_CMP:.*]] = icmp ne i64 %[[LHS_ADJ]], %[[RHS_ADJ]]
// LLVM-NEXT: %[[TMP:.*]] = and i1 %[[PTR_NULL]], %[[ADJ_CMP]]
// LLVM-NEXT: %{{.*}} = or i1 %[[TMP]], %[[PTR_CMP]]

// OGCG-LABEL: @_Z6cmp_neM3FooFviES1_
//      OGCG: %[[LHS_PTR:.*]] = extractvalue { i64, i64 } %{{.*}}, 0
// OGCG-NEXT: %[[RHS_PTR:.*]] = extractvalue { i64, i64 } %{{.*}}, 0
// OGCG-NEXT: %[[PTR_CMP:.*]] = icmp ne i64 %[[LHS_PTR]], %[[RHS_PTR]]
// OGCG-NEXT: %[[PTR_NULL:.*]] = icmp ne i64 %[[LHS_PTR]], 0
// OGCG-NEXT: %[[LHS_ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG-NEXT: %[[RHS_ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG-NEXT: %[[ADJ_CMP:.*]] = icmp ne i64 %[[LHS_ADJ]], %[[RHS_ADJ]]
// OGCG-NEXT: %[[TMP:.*]] = and i1 %[[PTR_NULL]], %[[ADJ_CMP]]
// OGCG-NEXT: %{{.*}} = or i1 %[[PTR_CMP]], %[[TMP]]

struct Bar {
  void m4();
};

bool memfunc_to_bool(void (Foo::*func)(int)) {
  return func;
}

// CIR-BEFORE-LABEL: @_Z15memfunc_to_boolM3FooFviE
// CIR-BEFORE:   %{{.+}} = cir.cast member_ptr_to_bool %{{.+}} : !cir.method<!cir.func<(!s32i)> in !rec_Foo> -> !cir.bool

// CIR-AFTER-LABEL: @_Z15memfunc_to_boolM3FooFviE
// CIR-AFTER:   %[[FUNC:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   %[[ZERO:.*]] = cir.const #cir.int<0> : !s64i
// CIR-AFTER:   %[[PTR:.*]] = cir.extract_member %[[FUNC]][0] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %{{.*}} = cir.cmp(ne, %[[PTR]], %[[ZERO]]) : !s64i, !cir.bool

// LLVM-LABEL: @_Z15memfunc_to_boolM3FooFviE
//      LLVM:   %[[MEMFUNC:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT:   %[[PTR:.*]] = extractvalue { i64, i64 } %[[MEMFUNC]], 0
// LLVM-NEXT:   %{{.*}} = icmp ne i64 %[[PTR]], 0

// OGCG-LABEL: @_Z15memfunc_to_boolM3FooFviE
//      OGCG:   %[[PTR:.*]] = extractvalue { i64, i64 } %{{.*}}, 0
// OGCG-NEXT:   %{{.*}} = icmp ne i64 %[[PTR]], 0

auto memfunc_reinterpret(void (Foo::*func)(int)) -> void (Bar::*)() {
  return reinterpret_cast<void (Bar::*)()>(func);
}

// CIR-BEFORE-LABEL: @_Z19memfunc_reinterpretM3FooFviE
// CIR-BEFORE:   %{{.+}} = cir.cast bitcast %{{.+}} : !cir.method<!cir.func<(!s32i)> in !rec_Foo> -> !cir.method<!cir.func<()> in !rec_Bar>

// CIR-AFTER-LABEL: @_Z19memfunc_reinterpretM3FooFviE
// CIR-AFTER:   %[[ARG:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   cir.store %[[ARG]], %{{.*}} : !rec_anon_struct1, !cir.ptr<!rec_anon_struct1>

// LLVM-LABEL: @_Z19memfunc_reinterpretM3FooFviE
//      LLVM:   %[[TMP:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT:   store { i64, i64 } %[[TMP]], ptr %{{.*}}

// OGCG-LABEL: @_Z19memfunc_reinterpretM3FooFviE
//      OGCG:   ret { i64, i64 } %{{.*}}

struct Base1 {
  int x;
  virtual void m1(int);
};

struct Base2 {
  int y;
  virtual void m2(int);
};

struct Derived : Base1, Base2 {
  virtual void m3(int);
};

using Base1MemFunc = void (Base1::*)(int);
using Base2MemFunc = void (Base2::*)(int);
using DerivedMemFunc = void (Derived::*)(int);

DerivedMemFunc base_to_derived_zero_offset(Base1MemFunc ptr) {
  return static_cast<DerivedMemFunc>(ptr);
}

// CIR-BEFORE-LABEL: @_Z27base_to_derived_zero_offsetM5Base1FviE
// CIR-BEFORE: %{{.+}} = cir.derived_method(%{{.+}} : !cir.method<!cir.func<(!s32i)> in !rec_Base1>) [0] -> !cir.method<!cir.func<(!s32i)> in !rec_Derived>

// CIR-AFTER-LABEL: @_Z27base_to_derived_zero_offsetM5Base1FviE
// CIR-AFTER:   %[[ARG:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   cir.store %[[ARG]], %{{.*}} : !rec_anon_struct1, !cir.ptr<!rec_anon_struct1>

// LLVM-LABEL: @_Z27base_to_derived_zero_offsetM5Base1FviE
//      LLVM:   %[[TMP:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT:   store { i64, i64 } %[[TMP]], ptr %{{.*}}

// OGCG-LABEL: @_Z27base_to_derived_zero_offsetM5Base1FviE
//      OGCG:   ret { i64, i64 } %{{.*}}

DerivedMemFunc base_to_derived(Base2MemFunc ptr) {
  return static_cast<DerivedMemFunc>(ptr);
}

// CIR-BEFORE-LABEL: @_Z15base_to_derivedM5Base2FviE
// CIR-BEFORE: %{{.+}} = cir.derived_method(%{{.+}} : !cir.method<!cir.func<(!s32i)> in !rec_Base2>) [16] -> !cir.method<!cir.func<(!s32i)> in !rec_Derived>

// CIR-AFTER-LABEL: @_Z15base_to_derivedM5Base2FviE
// CIR-AFTER:   %[[ARG:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   %[[ADJ:.*]] = cir.extract_member %[[ARG]][1] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %[[OFFSET:.*]] = cir.const #cir.int<16> : !s64i
// CIR-AFTER:   %[[NEW_ADJ:.*]] = cir.binop(add, %[[ADJ]], %[[OFFSET]]) : !s64i

// LLVM-LABEL: @_Z15base_to_derivedM5Base2FviE
//      LLVM: %[[ARG:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT: %[[ADJ:.*]] = extractvalue { i64, i64 } %[[ARG]], 1
// LLVM-NEXT: %[[NEW_ADJ:.*]] = add i64 %[[ADJ]], 16

// OGCG-LABEL: @_Z15base_to_derivedM5Base2FviE
//      OGCG: %[[ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG-NEXT: %[[NEW_ADJ:.*]] = add nsw i64 %[[ADJ]], 16
// OGCG-NEXT: %{{.*}} = insertvalue { i64, i64 } %{{.*}}, i64 %[[NEW_ADJ]], 1

Base1MemFunc derived_to_base_zero_offset(DerivedMemFunc ptr) {
  return static_cast<Base1MemFunc>(ptr);
}

// CIR-BEFORE-LABEL: @_Z27derived_to_base_zero_offsetM7DerivedFviE
// CIR-BEFORE: %{{.+}} = cir.base_method(%{{.+}} : !cir.method<!cir.func<(!s32i)> in !rec_Derived>) [0] -> !cir.method<!cir.func<(!s32i)> in !rec_Base1>

// CIR-AFTER-LABEL: @_Z27derived_to_base_zero_offsetM7DerivedFviE
// CIR-AFTER:   %[[ARG:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   cir.store %[[ARG]], %{{.*}} : !rec_anon_struct1, !cir.ptr<!rec_anon_struct1>

// LLVM-LABEL: @_Z27derived_to_base_zero_offsetM7DerivedFviE
//      LLVM:   %[[TMP:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT:   store { i64, i64 } %[[TMP]], ptr %{{.*}}

// OGCG-LABEL: @_Z27derived_to_base_zero_offsetM7DerivedFviE
//      OGCG:   ret { i64, i64 } %{{.*}}

Base2MemFunc derived_to_base(DerivedMemFunc ptr) {
  return static_cast<Base2MemFunc>(ptr);
}

// CIR-BEFORE-LABEL: @_Z15derived_to_baseM7DerivedFviE
// CIR-BEFORE: %{{.+}} = cir.base_method(%{{.+}} : !cir.method<!cir.func<(!s32i)> in !rec_Derived>) [16] -> !cir.method<!cir.func<(!s32i)> in !rec_Base2>

// CIR-AFTER-LABEL: @_Z15derived_to_baseM7DerivedFviE
// CIR-AFTER:   %[[ARG:.*]] = cir.load{{.*}} : !cir.ptr<!rec_anon_struct1>, !rec_anon_struct1
// CIR-AFTER:   %[[ADJ:.*]] = cir.extract_member %[[ARG]][1] : !rec_anon_struct1 -> !s64i
// CIR-AFTER:   %[[OFFSET:.*]] = cir.const #cir.int<16> : !s64i
// CIR-AFTER:   %[[NEW_ADJ:.*]] = cir.binop(sub, %[[ADJ]], %[[OFFSET]]) : !s64i

// LLVM-LABEL: @_Z15derived_to_baseM7DerivedFviE
//      LLVM: %[[ARG:.*]] = load { i64, i64 }, ptr %{{.*}}
// LLVM-NEXT: %[[ADJ:.*]] = extractvalue { i64, i64 } %[[ARG]], 1
// LLVM-NEXT: %[[NEW_ADJ:.*]] = sub i64 %[[ADJ]], 16

// OGCG-LABEL: @_Z15derived_to_baseM7DerivedFviE
//      OGCG: %[[ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG-NEXT: %[[NEW_ADJ:.*]] = sub nsw i64 %[[ADJ]], 16
// OGCG-NEXT: %{{.*}} = insertvalue { i64, i64 } %{{.*}}, i64 %[[NEW_ADJ]], 1

struct HasVTable {
  virtual void test(void (Foo::*)());
};

// Ensure that the vfunc pointer to the function involving a pointer-to-member-
// func could be emitted.
void HasVTable::test(void (Foo::*)()) {}

// CIR-BEFORE-LABEL: @_ZN9HasVTable4testEM3FooFvvE
// CIR-BEFORE:   cir.alloca !cir.method<!cir.func<()> in !rec_Foo>, !cir.ptr<!cir.method<!cir.func<()> in !rec_Foo>>
