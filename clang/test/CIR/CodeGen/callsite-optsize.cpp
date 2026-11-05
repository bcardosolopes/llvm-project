// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Oz -disable-llvm-passes -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OZ --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Oz -disable-llvm-passes -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OZ-OGCG --input-file=%t.ogcg.ll %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Os -disable-llvm-passes -fclangir -emit-llvm %s -o %t.os.ll
// RUN: FileCheck --check-prefix=OS --input-file=%t.os.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Os -disable-llvm-passes -emit-llvm %s -o %t.os.ogcg.ll
// RUN: FileCheck --check-prefix=OS-OGCG --input-file=%t.os.ogcg.ll %s

// Verify that -Oz propagates both minsize and optsize to callsite attributes,
// and -Os propagates optsize (without minsize), matching OGCG.

struct A { virtual ~A(); };

// -Oz: both minsize and optsize on the call site.
// OZ-LABEL: @_Z1fP1A
// OZ: call void %{{.*}}({{.*}}) [[OZ_CALL_ATTRS:#[0-9]+]]
// OZ-OGCG-LABEL: @_Z1fP1A
// OZ-OGCG: call void %{{.*}}({{.*}}) [[OZ_OGCG_CALL_ATTRS:#[0-9]+]]

// -Os: only optsize on the call site (no minsize).
// OS-LABEL: @_Z1fP1A
// OS: call void %{{.*}}({{.*}}) [[OS_CALL_ATTRS:#[0-9]+]]
// OS-OGCG-LABEL: @_Z1fP1A
// OS-OGCG: call void %{{.*}}({{.*}}) [[OS_OGCG_CALL_ATTRS:#[0-9]+]]
void f(A *a) { delete a; }

// OZ: attributes [[OZ_CALL_ATTRS]] = { minsize nounwind optsize }
// OZ-OGCG: attributes [[OZ_OGCG_CALL_ATTRS]] = { minsize nounwind optsize }

// OS: attributes [[OS_CALL_ATTRS]] = { nounwind optsize }
// OS-NOT: minsize
// OS-OGCG: attributes [[OS_OGCG_CALL_ATTRS]] = { nounwind optsize }
