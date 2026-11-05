// RUN: %clang_cc1 -x c++ -std=c++23 -debug-info-kind=limited -emit-llvm < %s | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -x c++ -std=c++23 -debug-info-kind=limited -fclangir -emit-llvm < %s | FileCheck %s --check-prefix=CIR %}

// OGCG-DAG: ![[FOO:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "Foo"
// OGCG-DAG: ![[BAR_SP:[0-9]+]] = distinct !DISubprogram(name: "bar", {{.*}}scope: ![[FOO]], {{.*}}type: ![[BAR_TYPE:[0-9]+]], {{.*}}flags: DIFlagPrototyped
// OGCG-DAG: ![[BAR_TYPE]] = !DISubroutineType(types: ![[BAR_ARGS:[0-9]+]])
// OGCG-DAG: ![[BAR_ARGS]] = !{null, ![[BAR_OBJ:[0-9]+]]}
// OGCG-DAG: ![[BAR_OBJ]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[FOO]], size: 64, flags: DIFlagArtificial | DIFlagObjectPointer)
// OGCG-DAG: ![[THIS_LOCAL:[0-9]+]] = !DILocalVariable(name: "this", arg: 1, scope: ![[BAR_SP]], {{.*}}flags: DIFlagArtificial | DIFlagObjectPointer)
// OGCG-DAG: ![[EXPL_SP:[0-9]+]] = distinct !DISubprogram(name: "explicit_this", {{.*}}scope: ![[FOO]], {{.*}}type: ![[EXPL_TYPE:[0-9]+]], {{.*}}flags: DIFlagPrototyped
// OGCG-DAG: ![[EXPL_TYPE]] = !DISubroutineType(types: ![[EXPL_ARGS:[0-9]+]])
// OGCG-DAG: ![[EXPL_ARGS]] = !{null, ![[EXPL_OBJ:[0-9]+]]}
// OGCG-DAG: ![[EXPL_OBJ]] = !DIDerivedType(tag: DW_TAG_rvalue_reference_type, baseType: ![[FOO]], size: 64, flags: DIFlagObjectPointer)
// OGCG-DAG: ![[EXPL_LOCAL:[0-9]+]] = !DILocalVariable(arg: 1, scope: ![[EXPL_SP]], {{.*}}flags: DIFlagObjectPointer)

// CIR: #dbg_declare(ptr %{{.*}}, ![[THIS_LOCAL:[0-9]+]], !DIExpression(),
// CIR: #dbg_declare(ptr %{{.*}}, ![[EXPL_LOCAL:[0-9]+]], !DIExpression(),
// CIR-DAG: ![[FOO:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "Foo"
// CIR-DAG: ![[BAR_SP:[0-9]+]] = distinct !DISubprogram(name: "bar", {{.*}}scope: ![[FOO]], {{.*}}type: ![[BAR_TYPE:[0-9]+]], {{.*}}flags: DIFlagPrototyped
// CIR-DAG: ![[BAR_TYPE]] = !DISubroutineType(types: ![[BAR_ARGS:[0-9]+]])
// CIR-DAG: ![[BAR_ARGS]] = !{null, ![[BAR_OBJ:[0-9]+]]}
// CIR-DAG: ![[BAR_OBJ]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[FOO]], size: 64, flags: DIFlagArtificial | DIFlagObjectPointer)
// CIR-DAG: ![[THIS_LOCAL]] = !DILocalVariable(name: "this", arg: 1, scope: ![[BAR_SP]], {{.*}}flags: DIFlagArtificial | DIFlagObjectPointer)
// CIR-DAG: ![[EXPL_SP:[0-9]+]] = distinct !DISubprogram(name: "explicit_this", {{.*}}scope: ![[FOO]], {{.*}}type: ![[EXPL_TYPE:[0-9]+]], {{.*}}flags: DIFlagPrototyped
// CIR-DAG: ![[EXPL_TYPE]] = !DISubroutineType(types: ![[EXPL_ARGS:[0-9]+]])
// CIR-DAG: ![[EXPL_ARGS]] = !{null, ![[EXPL_OBJ:[0-9]+]]}
// CIR-DAG: ![[EXPL_OBJ]] = !DIDerivedType(tag: DW_TAG_rvalue_reference_type, baseType: ![[FOO]], size: 64, flags: DIFlagObjectPointer)
// CIR-DAG: ![[EXPL_LOCAL]] = !DILocalVariable(arg: 1, scope: ![[EXPL_SP]], {{.*}}flags: DIFlagObjectPointer)

struct Foo {
  void bar() {}
  void explicit_this(this Foo &&) {}
};

void f() {
  Foo{}.bar();
  Foo{}.explicit_this();
}
