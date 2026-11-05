// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Per-parameter __attribute__((nonnull))
void foo(int * __attribute__((nonnull)) x) { *x = 0; }
// LLVM: define {{.*}}void @foo(ptr noundef nonnull %{{.*}})
// OGCG: define {{.*}}void @foo(ptr noundef nonnull %{{.*}})

// Function-level __attribute__((nonnull(1)))
void bar(int * x) __attribute__((nonnull(1)));
void bar(int * x) { *x = 0; }
// LLVM: define {{.*}}void @bar(ptr noundef nonnull %{{.*}})
// OGCG: define {{.*}}void @bar(ptr noundef nonnull %{{.*}})

// Selective nonnull — only 2nd param
void bar2(int * x, int * y) __attribute__((nonnull(2)));
void bar2(int * x, int * y) { *x = *y; }
// LLVM: define {{.*}}void @bar2(ptr noundef %{{.*}}, ptr noundef nonnull %{{.*}})
// OGCG: define {{.*}}void @bar2(ptr noundef %{{.*}}, ptr noundef nonnull %{{.*}})

// returns_nonnull (already working, included for completeness)
int *bar3(void) __attribute__((returns_nonnull));
int *bar3(void) { static int x; return &x; }
// LLVM: define {{.*}}nonnull ptr @bar3()
// OGCG: define {{.*}}nonnull ptr @bar3()

// Bare nonnull — all pointer params
void bar4(int n, int *p) __attribute__((nonnull));
void bar4(int n, int *p) { *p = n; }
// LLVM: define {{.*}}void @bar4(i32 noundef %{{.*}}, ptr noundef nonnull %{{.*}})
// OGCG: define {{.*}}void @bar4(i32 noundef %{{.*}}, ptr noundef nonnull %{{.*}})
