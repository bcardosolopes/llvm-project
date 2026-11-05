// RUN: %clang_cc1 -triple x86_64-linux -O1 -disable-llvm-passes -fclangir -emit-llvm -o - %s -no-pointer-tbaa | FileCheck %s --check-prefixes=CHECK,LLVM-NOPT
// RUN: %clang_cc1 -triple x86_64-linux -O1 -disable-llvm-passes -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-PTR
// RUN: %clang_cc1 -triple x86_64-linux -O1 -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR

// Check that direct-CIR preserves field-specific TBAA when loading a reference
// member from its containing struct. Prior to this load collapsed to a
// scalar pointer tag, losing the enclosing-struct access path.

struct S;

struct B {
  S &s;
  B(S &s);
  S &get();
};

B::B(S &s) : s(s) {
  // CHECK-LABEL: @_ZN1BC2ER1S
  // CHECK: store ptr {{.*}}, ptr %{{.*}}, align 8, !tbaa [[TAG_THIS:!.*]]
  // CHECK-NEXT: store ptr {{.*}}, ptr %{{.*}}, align 8, !tbaa [[TAG_S_PTR:!.*]]
  // CHECK: load ptr, ptr {{.*}}, !tbaa [[TAG_S_PTR]]
  // CHECK: store ptr {{.*}}, ptr {{.*}}, !tbaa [[TAG_S_PTR]]
}

S &B::get() {
  // CHECK-LABEL: @_ZN1B3getEv
  // CHECK: load ptr, ptr {{.*}}, !tbaa [[TAG_B_s:!.*]]
  return s;
}

// CIR-DAG: #tbaa[[ANY_PTR:[[:alnum:]_]+]] = #cir.tbaa_scalar<id = "any pointer", type = !cir.ptr<!void>>
// CIR-DAG: #tbaa[[S_PTR:[[:alnum:]_]+]] = #cir.tbaa_scalar<id = "p1 _ZTS1S", type = !cir.ptr<!rec_S>, parent = #tbaa[[ANY_PTR]]>
// CIR-DAG: #tbaa[[TYPE_B:[[:alnum:]_]+]] = #cir.tbaa_struct<id = "_ZTS1B", members = {<type_desc = #tbaa[[S_PTR]], offset = 0>}>
// CIR-DAG: #tbaa[[B_S:[[:alnum:]_]+]] = #cir.tbaa_tag<base = #tbaa[[TYPE_B]], access = #tbaa[[S_PTR]], offset = 0>
// CIR-LABEL: cir.func dso_local @_ZN1BC2ER1S
// CIR: cir.store align(8) %{{.*}}, %{{.*}} : !cir.ptr<!rec_S>, !cir.ptr<!cir.ptr<!rec_S>> tbaa(#tbaa[[S_PTR]])
// CIR-LABEL: cir.func dso_local @_ZN1B3getEv
// CIR: cir.load %{{.*}} : !cir.ptr<!cir.ptr<!rec_S>>, !cir.ptr<!rec_S> tbaa(#tbaa[[B_S]])

// LLVM-NOPT-DAG: [[TAG_S_PTR]] = !{[[TYPE_pointer:!.*]], [[TYPE_pointer]], i64 0}
// LLVM-NOPT-DAG: [[TAG_B_s]] = !{[[TYPE_B:!.*]], [[TYPE_pointer]], i64 0}
// LLVM-NOPT-DAG: [[TYPE_B]] = !{!"_ZTS1B", [[TYPE_pointer]], i64 0}
// LLVM-NOPT-DAG: [[TYPE_pointer]] = !{!"any pointer", [[TYPE_char:!.*]], i64 0}
// LLVM-NOPT-DAG: [[TYPE_char]] = !{!"omnipotent char", {{!.*}}, i64 0}

// LLVM-PTR-DAG: [[TAG_S_PTR]] = !{[[TYPE_S_PTR:!.*]], [[TYPE_S_PTR]], i64 0}
// LLVM-PTR-DAG: [[TAG_B_s]] = !{[[TYPE_B:!.*]], [[TYPE_S_PTR]], i64 0}
// LLVM-PTR-DAG: [[TYPE_B]] = !{!"_ZTS1B", [[TYPE_S_PTR]], i64 0}
// LLVM-PTR-DAG: [[TYPE_pointer:!.*]] = !{!"any pointer", [[TYPE_char:!.*]], i64 0}
// LLVM-PTR-DAG: [[TYPE_char]] = !{!"omnipotent char", {{!.*}}, i64 0}
// LLVM-PTR-DAG: [[TYPE_S_PTR]] = !{!"p1 _ZTS1S", [[TYPE_pointer]], i64 0}
