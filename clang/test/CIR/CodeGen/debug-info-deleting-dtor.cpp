// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// The virtual deleting destructor (D0) gets a proper DISubprogram, matching
// classic CodeGen: the source name "~V", the D0 linkage name, class scope,
// DIFlagPrototyped, and a declaration ref. CIR previously failed to associate
// the D0 function with its FunctionDecl (the destructor-name matcher only
// considered Dtor_Complete/Dtor_Base), so the deleting destructor was emitted
// with the mangled name as its DWARF name, file scope, and no flags. As a
// consequence its implicit `this` also lost DIFlagArtificial/ObjectPointer.

struct V {
  virtual ~V();
  virtual int f();
};
int V::f() { return 1; }
V::~V() {}
void sink(V *p) { delete p; }

// The deleting destructor subprogram carries the source name and D0 linkage.
// CHECK-DAG: distinct !DISubprogram(name: "~V", linkageName: "_ZN1VD0Ev",{{.*}}flags: DIFlagPrototyped, spFlags: DISPFlagDefinition,{{.*}}declaration: !{{[0-9]+}}
// Its implicit object parameter is artificial and an object pointer.
// CHECK-DAG: !DILocalVariable(name: "this", arg: 1,{{.*}}flags: DIFlagArtificial | DIFlagObjectPointer)

// OGCG-DAG: distinct !DISubprogram(name: "~V", linkageName: "_ZN1VD0Ev",{{.*}}flags: DIFlagPrototyped, spFlags: DISPFlagDefinition,{{.*}}declaration: !{{[0-9]+}}
// OGCG-DAG: !DILocalVariable(name: "this", arg: 1,{{.*}}flags: DIFlagArtificial | DIFlagObjectPointer)
