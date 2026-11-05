// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

struct S {
    int x;
};

// LLVM: define dso_local void @zeroInit
// LLVM: [[TMP0:%.*]] = alloca %struct.S, i64 1
// LLVM: call void @llvm.memset.p0.i64(ptr{{.*}} [[TMP0]], i8 0, i64 4, i1 false)
void zeroInit() {
  struct S s = {0};
}
