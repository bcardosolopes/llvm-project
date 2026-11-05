// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=constructor -dwarf-version=4 -std=c++17 %s -o %t.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -emit-llvm \
// RUN:   -debug-info-kind=constructor -dwarf-version=4 -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that temporaries built for a default argument inherit the call-site
// location of `use();`, not the declaration site of the default argument.

struct S {
  S(int x = 0);
  ~S();
};

void use(S s = S());

void test() {
  use();
}

// CIR-LABEL: define {{.*}}@_Z4testv()
// CIR-SAME: !dbg ![[TEST_SP:[0-9]+]]
// CIR: call void @_ZN1SC1Ei(ptr noundef nonnull align 1 dereferenceable(1) {{.*}}), !dbg ![[CALLSITE:[0-9]+]]
// CIR: call void @_Z3use1S(ptr noundef {{.*}}), !dbg ![[CALLSITE]]
// CIR: call void @_ZN1SD1Ev(ptr noundef nonnull align 1 dereferenceable(1) {{.*}}){{.*}}, !dbg ![[CALLSITE]]
// CIR: ![[TEST_SP]] = distinct !DISubprogram(name: "test", linkageName: "_Z4testv"
// CIR: ![[CALLSITE]] = !DILocation(line: 19, column: 3, scope: ![[TEST_SP]])

// OGCG-LABEL: define {{.*}}@_Z4testv()
// OGCG-SAME: !dbg ![[TEST_SP:[0-9]+]]
// OGCG: call void @_ZN1SC1Ei(ptr noundef nonnull align 1 dereferenceable(1) {{.*}}), !dbg ![[CALLSITE:[0-9]+]]
// OGCG: call void @_Z3use1S(ptr noundef {{.*}}), !dbg ![[CALLSITE]]
// OGCG: call void @_ZN1SD1Ev(ptr noundef nonnull align 1 dereferenceable(1) {{.*}}){{.*}}, !dbg ![[CALLSITE]]
// OGCG: ![[TEST_SP]] = distinct !DISubprogram(name: "test", linkageName: "_Z4testv"
// OGCG: ![[CALLSITE]] = !DILocation(line: 19, column: 3, scope: ![[TEST_SP]])
