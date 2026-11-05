// Tests that TBAA metadata is correct when a lambda captures a struct by
// reference and accesses a member field. Previously, the TBAA base type was
// incorrectly set to the lambda closure struct instead of the actual struct
// being accessed, causing an LLVM TBAA verifier crash:
// "Did not see access type in access path!"

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -std=c++17 -O1 -no-pointer-tbaa
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -std=c++17 -O1 -disable-llvm-passes -no-pointer-tbaa
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

struct S {
    int id;
};

// CIR: #tbaa[[DOUBLE:.*]] = #cir.tbaa_scalar<id = "double", type = !cir.double>
// CIR: #tbaa[[INT:.*]] = #cir.tbaa_scalar<id = "int", type = !s32i>
// CIR: #tbaa[[STRUCT_S:.*]] = #cir.tbaa_struct<id = "_ZTS1S", members = {<type_desc = #tbaa[[INT]], offset = 0>}>
// CIR: #tbaa[[STRUCT_Multi:.*]] = #cir.tbaa_struct<id = "_ZTS5Multi", members = {<type_desc = #tbaa[[INT]], offset = 0>, <type_desc = #tbaa[[DOUBLE]], offset = 8>}>
// CIR: #tbaa[[TAG_S_id:.*]] = #cir.tbaa_tag<base = #tbaa[[STRUCT_S]], access = #tbaa[[INT]], offset = 0>
// CIR: #tbaa[[TAG_Multi_y:.*]] = #cir.tbaa_tag<base = #tbaa[[STRUCT_Multi]], access = #tbaa[[DOUBLE]], offset = 8>

// The TBAA base must be struct S (_ZTS1S), NOT the lambda closure struct.
int use_ref_capture() {
    S s{42};
    auto f = [&s]() { return s.id; };
    return f();
}
// CIR-LABEL: cir.func {{.*}} @_ZZ15use_ref_capturevENK3$_0clEv
// CIR: cir.load align(4) %{{.*}} : !cir.ptr<!s32i>, !s32i tbaa(#tbaa[[TAG_S_id]])

// LLVM: define internal {{.*}} @"_ZZ15use_ref_capturevENK3$_0clEv"
// LLVM: load i32, ptr %{{.*}}, align 4, !tbaa [[TBAA_S_id:!.*]]

struct Multi {
    int x;
    double y;
};

// The TBAA base must be struct Multi (_ZTS5Multi), NOT the lambda closure struct.
double multi_field_ref_capture() {
    Multi m{1, 2.0};
    auto f = [&m]() { return m.y; };
    return f();
}
// CIR-LABEL: cir.func {{.*}} @_ZZ23multi_field_ref_capturevENK3$_0clEv
// CIR: cir.load align(8) %{{.*}} : !cir.ptr<!cir.double>, !cir.double tbaa(#tbaa[[TAG_Multi_y]])

// LLVM: define internal {{.*}} @"_ZZ23multi_field_ref_capturevENK3$_0clEv"
// LLVM: load double, ptr %{{.*}}, align 8, !tbaa [[TBAA_Multi_y:!.*]]

// LLVM-DAG: [[TBAA_S_id]] = !{[[TYPE_S:!.*]], [[TYPE_INT:!.*]], i64 0}
// LLVM-DAG: [[TYPE_S]] = !{!"_ZTS1S", [[TYPE_INT]], i64 0}
// LLVM-DAG: [[TYPE_INT]] = !{!"int", [[TYPE_CHAR:!.*]], i64 0}
// LLVM-DAG: [[TYPE_CHAR]] = !{!"omnipotent char", [[TBAA_ROOT:!.*]], i64 0}
// LLVM-DAG: [[TBAA_ROOT]] = !{!"Simple C++ TBAA"}
// LLVM-DAG: [[TBAA_Multi_y]] = !{[[TYPE_Multi:!.*]], [[TYPE_DOUBLE:!.*]], i64 8}
// LLVM-DAG: [[TYPE_Multi]] = !{!"_ZTS5Multi", [[TYPE_INT]], i64 0, [[TYPE_DOUBLE]], i64 8}
// LLVM-DAG: [[TYPE_DOUBLE]] = !{!"double", [[TYPE_CHAR]], i64 0}
