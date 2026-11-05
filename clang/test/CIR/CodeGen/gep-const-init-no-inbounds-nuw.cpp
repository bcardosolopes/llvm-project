// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// CIR now correctly emits a plain 'getelementptr' without inbounds or
// nuw flags in constant initializers, matching OGCG behavior.

struct Data {
  unsigned *data;
  unsigned array[1];
};

// CHECK: @shared_null = {{.*}}global %struct.Data { ptr getelementptr (i8, ptr @shared_null, i64 8)
Data shared_null = { shared_null.array };
