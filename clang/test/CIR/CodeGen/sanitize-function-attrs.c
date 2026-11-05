// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fsanitize=thread %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fsanitize=thread %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

__attribute__((no_sanitize("thread")))
int no_tsan(int *p) { return *p + 1; }

int yes_tsan(int *p) { return *p + 2; }

__attribute__((disable_sanitizer_instrumentation))
int no_inst(int *p) { return *p + 3; }

// LLVM: define dso_local i32 @no_tsan(ptr noundef {{.*}}) [[NO_TSAN_ATTR:#[0-9]+]]
// LLVM: define dso_local i32 @yes_tsan(ptr noundef {{.*}}) [[YES_TSAN_ATTR:#[0-9]+]]
// LLVM: define dso_local i32 @no_inst(ptr noundef {{.*}}) [[NO_INST_ATTR:#[0-9]+]]
// LLVM-NOT: attributes [[NO_TSAN_ATTR]] = { {{.*}}sanitize_thread{{.*}} }
// LLVM: attributes [[YES_TSAN_ATTR]] = { {{.*}}sanitize_thread{{.*}} }
// LLVM: attributes [[NO_INST_ATTR]] = { {{.*}}disable_sanitizer_instrumentation{{.*}} }
// LLVM-NOT: attributes [[NO_INST_ATTR]] = { {{.*}}sanitize_thread{{.*}} }

// OGCG: define dso_local i32 @no_tsan(ptr noundef {{.*}}) [[NO_TSAN_ATTR:#[0-9]+]]
// OGCG: define dso_local i32 @yes_tsan(ptr noundef {{.*}}) [[YES_TSAN_ATTR:#[0-9]+]]
// OGCG: define dso_local i32 @no_inst(ptr noundef {{.*}}) [[NO_INST_ATTR:#[0-9]+]]
// OGCG-NOT: attributes [[NO_TSAN_ATTR]] = { {{.*}}sanitize_thread{{.*}} }
// OGCG: attributes [[YES_TSAN_ATTR]] = { {{.*}}sanitize_thread{{.*}} }
// OGCG: attributes [[NO_INST_ATTR]] = { {{.*}}disable_sanitizer_instrumentation{{.*}} }
// OGCG-NOT: attributes [[NO_INST_ATTR]] = { {{.*}}sanitize_thread{{.*}} }
