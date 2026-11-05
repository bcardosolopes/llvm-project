// RUN: %clang -target x86_64-unknown-linux-gnu -S -emit-llvm -O0 \
// RUN:   -fsanitize=array-bounds -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang -target x86_64-unknown-linux-gnu -S -emit-llvm -O0 \
// RUN:   -fsanitize=array-bounds %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s

int arr[2];

void out_of_bounds(int i) {
  arr[i] = 0;
}

// CHECK-LABEL: define dso_local void @out_of_bounds(
// CHECK: call void @__ubsan_handle_out_of_bounds(ptr {{.*}}, i64 {{.*}}) [[CALL_ATTR:#[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
// CHECK: ret void
//
// CHECK: attributes [[CALL_ATTR]] = { nomerge nounwind }
