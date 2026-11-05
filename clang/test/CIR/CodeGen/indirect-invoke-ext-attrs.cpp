// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fdevirtualize-speculatively -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.flat.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fdevirtualize-speculatively -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fdevirtualize-speculatively -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

struct A {
  virtual signed char vf(signed char x);
};

signed char A::vf(signed char x) { return x; }

void may_throw();
typedef signed char (*signed_char_fn_t)(signed char);

signed char call_indirect(signed_char_fn_t fn, signed char v) {
  try {
    return fn(v);
  } catch (...) {
    return 0;
  }
}

signed char call_virtual(A *a, signed char v) {
  try {
    return a->vf(v);
  } catch (...) {
    return 0;
  }
}

signed char catch_indirect(signed_char_fn_t fn, signed char v) {
  try {
    may_throw();
  } catch (...) {
    return fn(v);
  }
  __builtin_unreachable();
}

// CIR-LABEL: cir.func{{.*}} @_Z13call_indirectPFaaEa
// CIR: %[[FN:.*]] = cir.load{{.*}} : {{.*}} !cir.ptr<!cir.func<(!s8i) -> !s8i>>
// CIR: %[[ARG:.*]] = cir.load{{.*}} : {{.*}} !s8i
// CIR: %[[RES:.*]] = cir.try_call %[[FN]](%[[ARG]]) {{.*}} {cir.call_arg_attrs = [{cir.noundef, cir.signext}], cir.call_res_attrs = {cir.noundef, cir.signext}} : (!cir.ptr<!cir.func<(!s8i) -> !s8i>>, !s8i) -> !s8i

// CIR-LABEL: cir.func{{.*}} @_Z12call_virtualP1Aa
// CIR: %[[THIS:.*]] = cir.load{{.*}} : {{.*}} !cir.ptr<!rec_A>
// CIR: %[[VARG:.*]] = cir.load{{.*}} : {{.*}} !s8i
// CIR: %[[VFN:.*]] = cir.load{{.*}} : {{.*}} !cir.ptr<!cir.func<(!cir.ptr<!rec_A>, !s8i) -> !s8i>>
// CIR: %[[VRES:.*]] = cir.try_call %[[VFN]](%[[THIS]], %[[VARG]]) {{.*}} {cir.call_arg_attrs = [{cir.align = 8 : i64, cir.dereferenceable = 8 : i64, cir.nonnull, cir.noundef}, {cir.noundef, cir.signext}], cir.call_res_attrs = {cir.noundef, cir.signext}, cir.virtual.typeid = "_ZTS1A", cir.virtual.typetest_kind = "public"} : (!cir.ptr<!cir.func<(!cir.ptr<!rec_A>, !s8i) -> !s8i>>, !cir.ptr<!rec_A>, !s8i) -> !s8i

// CIR-LABEL: cir.func{{.*}} @_Z14catch_indirectPFaaEa
// CIR: cir.try_call @_Z9may_throwv() {{.*}} : () -> ()
// CIR: %[[CFN:.*]] = cir.load{{.*}} : {{.*}} !cir.ptr<!cir.func<(!s8i) -> !s8i>>
// CIR: %[[CARG:.*]] = cir.load{{.*}} : {{.*}} !s8i
// CIR: %[[CRES:.*]] = cir.try_call %[[CFN]](%[[CARG]]) {{.*}} {cir.call_arg_attrs = [{cir.noundef, cir.signext}], cir.call_res_attrs = {cir.noundef, cir.signext}} : (!cir.ptr<!cir.func<(!s8i) -> !s8i>>, !s8i) -> !s8i

// LLVM-LABEL: define dso_local noundef signext i8 @_Z13call_indirectPFaaEa
// LLVM: invoke noundef signext i8 %{{.*}}(i8 noundef signext %{{.*}})

// LLVM-LABEL: define dso_local noundef signext i8 @_Z12call_virtualP1Aa
// LLVM: call i1 @llvm.public.type.test(ptr %{{[^,]+}}, metadata !"_ZTS1A")
// LLVM-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
// LLVM: invoke noundef signext i8 %{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}, i8 noundef signext %{{.*}})

// LLVM-LABEL: define dso_local noundef signext i8 @_Z14catch_indirectPFaaEa
// LLVM: invoke void @_Z9may_throwv()
// LLVM: invoke noundef signext i8 %{{.*}}(i8 noundef signext %{{.*}})

// OGCG-LABEL: define{{.*}} noundef signext i8 @_Z13call_indirectPFaaEa
// OGCG: invoke noundef signext i8 %{{.*}}(i8 noundef signext %{{.*}})

// OGCG-LABEL: define{{.*}} noundef signext i8 @_Z12call_virtualP1Aa
// OGCG: call i1 @llvm.public.type.test(ptr %{{[^,]+}}, metadata !"_ZTS1A")
// OGCG-NEXT: call void @llvm.assume(i1 %{{[^)]+}})
// OGCG: invoke noundef signext i8 %{{.*}}(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}, i8 noundef signext %{{.*}})

// OGCG-LABEL: define{{.*}} noundef signext i8 @_Z14catch_indirectPFaaEa
// OGCG: invoke void @_Z9may_throwv()
// OGCG: invoke noundef signext i8 %{{.*}}(i8 noundef signext %{{.*}})
