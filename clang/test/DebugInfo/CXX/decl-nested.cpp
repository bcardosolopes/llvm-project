// RUN: %clang_cc1 -std=c++11 -debug-info-kind=standalone -emit-llvm -triple x86_64-apple-darwin %s -o %t
// RUN: cat %t | FileCheck %s -check-prefix=CHECK0
// RUN: cat %t | FileCheck %s -check-prefix=CHECK1
// RUN: cat %t | FileCheck %s -check-prefix=CHECK2
// RUN: %if cir-support %{ %clang_cc1 -std=c++11 -debug-info-kind=standalone -emit-llvm -triple x86_64-apple-darwin -fclangir %s -o %t-cir %}
// RUN: %if cir-support %{ cat %t-cir | FileCheck %s -check-prefix=CHECK0-CIR %}
// RUN: %if cir-support %{ cat %t-cir | FileCheck %s -check-prefix=CHECK1-CIR %}
// RUN: %if cir-support %{ cat %t-cir | FileCheck %s -check-prefix=CHECK2-CIR %}
//
// This test ensures that we associate a declaration with the
// definition of the constructor for OuterClass. The declaration is
// necessary so the backend can emit the DW_AT_specification attribute
// for the definition.

class Foo;
class OuterClass
{
  static class InnerClass {
  public:
    InnerClass(); // Here createContextChain() generates a limited type for OuterClass.
  } theInnerClass;
// CHECK0-CIR: !DIGlobalVariable(name: "theInnerClass", linkageName: "_ZN10OuterClass13theInnerClassE"{{.*}}type: ![[INNER:[0-9]+]]{{.*}}declaration: ![[MEMBER:[0-9]+]]
// CHECK0-CIR: ![[INNER]] = distinct !DICompositeType(tag: DW_TAG_class_type, name: "InnerClass", scope: ![[OUTER:[0-9]+]]
// CHECK0-CIR: ![[OUTER]] = distinct !DICompositeType(tag: DW_TAG_class_type, name: "OuterClass"
// CHECK0-CIR-DAG: ![[MEMBER]] = !DIDerivedType(tag: DW_TAG_member, name: "theInnerClass", scope: ![[OUTER]]{{.*}}baseType: ![[INNER]]
// CHECK0-CIR-DAG: ![[DECL:[0-9]+]] = !DISubprogram(name: "OuterClass", linkageName: "_ZN10OuterClassC4EPK3Foo", scope: ![[OUTER]]{{.*}}spFlags: 0)
// CHECK0-CIR-DAG: distinct !DICompositeType(tag: DW_TAG_class_type, name: "Foo",{{.*}}flags: DIFlagFwdDecl | DIFlagNonTrivial, identifier: "_ZTS3Foo")
// CHECK0-CIR: !DISubprogram(name: "OuterClass", linkageName: "_ZN10OuterClassC2EPK3Foo", scope: ![[OUTER]]{{.*}}spFlags: DISPFlagDefinition{{.*}}declaration: ![[DECL]]
// CHECK0-CIR: !DISubprogram(name: "OuterClass", linkageName: "_ZN10OuterClassC1EPK3Foo", scope: ![[OUTER]]{{.*}}spFlags: DISPFlagDefinition{{.*}}declaration: ![[DECL]]
// CHECK0: ![[DECL:[0-9]+]] = !DISubprogram(name: "OuterClass"
// CHECK0-SAME: line: [[@LINE+2]]
// CHECK0-SAME: spFlags: 0
  OuterClass(const Foo *); // line 10
};
OuterClass::InnerClass OuterClass::theInnerClass; // This toplevel decl causes InnerClass to be generated.
// CHECK0: !DISubprogram(name: "OuterClass"
// CHECK0-SAME: line: [[@LINE+3]]
// CHECK0-SAME: DISPFlagDefinition
// CHECK0-SAME: declaration: ![[DECL]]
OuterClass::OuterClass(const Foo *meta) { } // line 13






class Foo1;
class OuterClass1
{
  static class InnerClass1 {
  public:
    InnerClass1();
  } theInnerClass1;
// CHECK1-CIR: !DIGlobalVariable(name: "theInnerClass1", linkageName: "_ZN11OuterClass114theInnerClass1E"{{.*}}type: ![[INNER:[0-9]+]]{{.*}}declaration: ![[MEMBER:[0-9]+]]
// CHECK1-CIR: ![[INNER]] = distinct !DICompositeType(tag: DW_TAG_class_type, name: "InnerClass1", scope: ![[OUTER:[0-9]+]]
// CHECK1-CIR: ![[OUTER]] = distinct !DICompositeType(tag: DW_TAG_class_type, name: "OuterClass1"
// CHECK1-CIR-DAG: ![[MEMBER]] = !DIDerivedType(tag: DW_TAG_member, name: "theInnerClass1", scope: ![[OUTER]]{{.*}}baseType: ![[INNER]]
// CHECK1-CIR-DAG: ![[DECL:[0-9]+]] = !DISubprogram(name: "Bar", linkageName: "_ZN11OuterClass13BarEPK4Foo1", scope: ![[OUTER]]{{.*}}spFlags: 0)
// CHECK1-CIR-DAG: distinct !DICompositeType(tag: DW_TAG_class_type, name: "Foo1",{{.*}}flags: DIFlagFwdDecl | DIFlagNonTrivial, identifier: "_ZTS4Foo1")
// CHECK1-CIR: !DISubprogram(name: "Bar", linkageName: "_ZN11OuterClass13BarEPK4Foo1", scope: ![[OUTER]]{{.*}}spFlags: DISPFlagDefinition{{.*}}declaration: ![[DECL]]
// CHECK1: ![[DECL:[0-9]+]] = !DISubprogram(name: "Bar"
// CHECK1-SAME: line: [[@LINE+2]]
// CHECK1-SAME: spFlags: 0
  void Bar(const Foo1 *);
};
OuterClass1::InnerClass1 OuterClass1::theInnerClass1;
// CHECK1: !DISubprogram(name: "Bar"
// CHECK1-SAME: line: [[@LINE+3]]
// CHECK1-SAME: DISPFlagDefinition
// CHECK1-SAME: declaration: ![[DECL]]
void OuterClass1::Bar(const Foo1 *meta) { }





class Foo2;
class OuterClass2
{
  static class InnerClass2 {
  public:
    InnerClass2();
  } theInnerClass2;
// CHECK2-CIR: !DIGlobalVariable(name: "theInnerClass2", linkageName: "_ZN11OuterClass214theInnerClass2E"{{.*}}type: ![[INNER:[0-9]+]]{{.*}}declaration: ![[MEMBER:[0-9]+]]
// CHECK2-CIR: ![[INNER]] = distinct !DICompositeType(tag: DW_TAG_class_type, name: "InnerClass2", scope: ![[OUTER:[0-9]+]]
// CHECK2-CIR: ![[OUTER]] = distinct !DICompositeType(tag: DW_TAG_class_type, name: "OuterClass2"
// CHECK2-CIR-DAG: ![[MEMBER]] = !DIDerivedType(tag: DW_TAG_member, name: "theInnerClass2", scope: ![[OUTER]]{{.*}}baseType: ![[INNER]]
// CHECK2-CIR-DAG: ![[DECL:[0-9]+]] = !DISubprogram(name: "~OuterClass2", linkageName: "_ZN11OuterClass2D4Ev", scope: ![[OUTER]]{{.*}}spFlags: 0)
// CHECK2-CIR: !DISubprogram(name: "~OuterClass2", linkageName: "_ZN11OuterClass2D2Ev", scope: ![[OUTER]]{{.*}}spFlags: DISPFlagDefinition{{.*}}declaration: ![[DECL]]
// CHECK2-CIR: !DISubprogram(name: "~OuterClass2", linkageName: "_ZN11OuterClass2D1Ev", scope: ![[OUTER]]{{.*}}spFlags: DISPFlagDefinition{{.*}}declaration: ![[DECL]]
// CHECK2: ![[DECL:[0-9]+]] = !DISubprogram(name: "~OuterClass2"
// CHECK2-SAME: line: [[@LINE+2]]
// CHECK2-SAME: spFlags: 0
  ~OuterClass2(); // line 10
};
OuterClass2::InnerClass2 OuterClass2::theInnerClass2;
// CHECK2: !DISubprogram(name: "~OuterClass2"
// CHECK2-SAME: line: [[@LINE+3]]
// CHECK2-SAME: DISPFlagDefinition
// CHECK2-SAME: declaration: ![[DECL]]
OuterClass2::~OuterClass2() { }
