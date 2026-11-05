// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir \
// RUN:   -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify direct-CIR and OGCG both emit structured-binding locals on the
// decomposition storage with the expected debug expressions and source-shaped
// typedef types.

using MyInt = int;

struct S {
  int x;
};

struct U {
  S s;
  MyInt n;
};

void record_binding() {
  U u{{1}, 2};
  auto [s, n] = u;
  (void)s;
  (void)n;
}

void ref_binding(U &u) {
  auto &[s, n] = u;
  (void)s;
  (void)n;
}

void array_binding() {
  MyInt a[2] = {1, 2};
  auto [x, y] = a;
  (void)x;
  (void)y;
}

// CIR-LABEL: define{{.*}} @_Z14record_bindingv
// CIR: %{{.*}} = alloca %struct.U
// CIR: [[CIR_REC_SLOT:%.*]] = alloca %struct.U
// CIR: #dbg_declare(ptr [[CIR_REC_SLOT]], !{{[0-9]+}}, !DIExpression(), !{{[0-9]+}})
// CIR: #dbg_declare(ptr [[CIR_REC_SLOT]], !{{[0-9]+}}, !DIExpression(DW_OP_plus_uconst, 4), !{{[0-9]+}})

// CIR-LABEL: define{{.*}} @_Z11ref_bindingR1U
// CIR: %{{.*}} = alloca ptr
// CIR: [[CIR_REF_SLOT:%.*]] = alloca ptr
// CIR: #dbg_declare(ptr [[CIR_REF_SLOT]], !{{[0-9]+}}, !DIExpression(DW_OP_deref), !{{[0-9]+}})
// CIR: #dbg_declare(ptr [[CIR_REF_SLOT]], !{{[0-9]+}}, !DIExpression(DW_OP_deref, DW_OP_plus_uconst, 4), !{{[0-9]+}})

// CIR-LABEL: define{{.*}} @_Z13array_bindingv
// CIR: %{{.*}} = alloca [2 x i32]
// CIR: [[CIR_ARR_SLOT:%.*]] = alloca [2 x i32]
// CIR: #dbg_declare(ptr [[CIR_ARR_SLOT]], !{{[0-9]+}}, !DIExpression(), !{{[0-9]+}})
// CIR: #dbg_declare(ptr [[CIR_ARR_SLOT]], !{{[0-9]+}}, !DIExpression(DW_OP_plus_uconst, 4), !{{[0-9]+}})
// CIR-DAG: ![[CIR_INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: ![[CIR_MYINT:[0-9]+]] = !DIDerivedType(tag: DW_TAG_typedef, name: "MyInt", file: !{{[0-9]+}}, line: {{[0-9]+}}, baseType: ![[CIR_INT]])
// CIR-DAG: ![[CIR_REC_SCOPE:[0-9]+]] = distinct !DISubprogram(name: "record_binding"
// CIR-DAG: !DILocalVariable(name: "s", scope: ![[CIR_REC_SCOPE]], {{.*}}type: ![[CIR_S_TYPE:[0-9]+]])
// CIR-DAG: ![[CIR_S_TYPE]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S", {{.*}}size: 32
// CIR-DAG: !DILocalVariable(name: "n", scope: ![[CIR_REC_SCOPE]], {{.*}}type: ![[CIR_MYINT]])
// CIR-DAG: ![[CIR_REF_SCOPE:[0-9]+]] = distinct !DISubprogram(name: "ref_binding"
// CIR-DAG: !DILocalVariable(name: "s", scope: ![[CIR_REF_SCOPE]], {{.*}}type: ![[CIR_S_TYPE]])
// CIR-DAG: !DILocalVariable(name: "n", scope: ![[CIR_REF_SCOPE]], {{.*}}type: ![[CIR_MYINT]])
// CIR-DAG: ![[CIR_ARR_SCOPE:[0-9]+]] = distinct !DISubprogram(name: "array_binding"
// CIR-DAG: !DILocalVariable(name: "x", scope: ![[CIR_ARR_SCOPE]], {{.*}}type: ![[CIR_MYINT]])
// CIR-DAG: !DILocalVariable(name: "y", scope: ![[CIR_ARR_SCOPE]], {{.*}}type: ![[CIR_MYINT]])

// OGCG-LABEL: define{{.*}} @_Z14record_bindingv
// OGCG: %{{.*}} = alloca %struct.U
// OGCG: [[OGCG_REC_SLOT:%.*]] = alloca %struct.U
// OGCG: #dbg_declare(ptr [[OGCG_REC_SLOT]], !{{[0-9]+}}, !DIExpression(), !{{[0-9]+}})
// OGCG: #dbg_declare(ptr [[OGCG_REC_SLOT]], !{{[0-9]+}}, !DIExpression(DW_OP_plus_uconst, 4), !{{[0-9]+}})

// OGCG-LABEL: define{{.*}} @_Z11ref_bindingR1U
// OGCG: %{{.*}} = alloca ptr
// OGCG: [[OGCG_REF_SLOT:%.*]] = alloca ptr
// OGCG: #dbg_declare(ptr [[OGCG_REF_SLOT]], !{{[0-9]+}}, !DIExpression(DW_OP_deref), !{{[0-9]+}})
// OGCG: #dbg_declare(ptr [[OGCG_REF_SLOT]], !{{[0-9]+}}, !DIExpression(DW_OP_deref, DW_OP_plus_uconst, 4), !{{[0-9]+}})

// OGCG-LABEL: define{{.*}} @_Z13array_bindingv
// OGCG: %{{.*}} = alloca [2 x i32]
// OGCG: [[OGCG_ARR_SLOT:%.*]] = alloca [2 x i32]
// OGCG: #dbg_declare(ptr [[OGCG_ARR_SLOT]], !{{[0-9]+}}, !DIExpression(), !{{[0-9]+}})
// OGCG: #dbg_declare(ptr [[OGCG_ARR_SLOT]], !{{[0-9]+}}, !DIExpression(DW_OP_plus_uconst, 4), !{{[0-9]+}})
// OGCG-DAG: ![[OGCG_INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: ![[OGCG_MYINT:[0-9]+]] = !DIDerivedType(tag: DW_TAG_typedef, name: "MyInt", file: !{{[0-9]+}}, line: {{[0-9]+}}, baseType: ![[OGCG_INT]])
// OGCG-DAG: ![[OGCG_REC_SCOPE:[0-9]+]] = distinct !DISubprogram(name: "record_binding"
// OGCG-DAG: !DILocalVariable(name: "s", scope: ![[OGCG_REC_SCOPE]], {{.*}}type: ![[OGCG_S_TYPE:[0-9]+]])
// OGCG-DAG: ![[OGCG_S_TYPE]] = {{(distinct )?}}!DICompositeType(tag: DW_TAG_structure_type, name: "S", {{.*}}size: 32
// OGCG-DAG: !DILocalVariable(name: "n", scope: ![[OGCG_REC_SCOPE]], {{.*}}type: ![[OGCG_MYINT]])
// OGCG-DAG: ![[OGCG_REF_SCOPE:[0-9]+]] = distinct !DISubprogram(name: "ref_binding"
// OGCG-DAG: !DILocalVariable(name: "s", scope: ![[OGCG_REF_SCOPE]], {{.*}}type: ![[OGCG_S_TYPE]])
// OGCG-DAG: !DILocalVariable(name: "n", scope: ![[OGCG_REF_SCOPE]], {{.*}}type: ![[OGCG_MYINT]])
// OGCG-DAG: ![[OGCG_ARR_SCOPE:[0-9]+]] = distinct !DISubprogram(name: "array_binding"
// OGCG-DAG: !DILocalVariable(name: "x", scope: ![[OGCG_ARR_SCOPE]], {{.*}}type: ![[OGCG_MYINT]])
// OGCG-DAG: !DILocalVariable(name: "y", scope: ![[OGCG_ARR_SCOPE]], {{.*}}type: ![[OGCG_MYINT]])
