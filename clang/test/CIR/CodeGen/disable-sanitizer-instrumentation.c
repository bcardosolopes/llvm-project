// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

__attribute__((disable_sanitizer_instrumentation))
void no_sanitize(void) {}

void with_sanitize(void) {}

// LLVM: define dso_local void @no_sanitize() [[ATTR_NO:#[0-9]+]]
// LLVM: define dso_local void @with_sanitize() [[ATTR_YES:#[0-9]+]]
// LLVM: attributes [[ATTR_NO]] = { {{.*}}disable_sanitizer_instrumentation{{.*}} }
// LLVM-NOT: attributes [[ATTR_YES]] = { {{.*}}disable_sanitizer_instrumentation{{.*}} }

// OGCG: define dso_local void @no_sanitize() [[ATTR_NO:#[0-9]+]]
// OGCG: define dso_local void @with_sanitize() [[ATTR_YES:#[0-9]+]]
// OGCG: attributes [[ATTR_NO]] = { {{.*}}disable_sanitizer_instrumentation{{.*}} }
// OGCG-NOT: attributes [[ATTR_YES]] = { {{.*}}disable_sanitizer_instrumentation{{.*}} }
