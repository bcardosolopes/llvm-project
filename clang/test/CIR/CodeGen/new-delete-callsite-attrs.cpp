// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CHECK --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=CHECK --input-file=%t.og.ll %s

// Verify CIR and OGCG emit matching call-site attrs for operator new[]/delete[].

int test() {
  int *arr = new int[10];
  arr[0] = 42;
  int r = arr[0];
  delete[] arr;
  return r;
}

// CHECK: = call noalias noundef nonnull ptr @_Znam(i64 noundef 40) #[[NEW_CALL:[0-9]+]]
// CHECK: call void @_ZdaPv(ptr noundef {{.*}}) #[[DEL_CALL:[0-9]+]]
// CHECK: attributes #[[NEW_CALL]] = { builtin allocsize(0) }
// CHECK: attributes #[[DEL_CALL]] = { builtin nounwind }
