// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s

typedef int v8i __attribute__((ext_vector_type(8)));
typedef _Bool v8b __attribute__((ext_vector_type(8)));

// CHECK-LABEL: @test_load(
// CHECK: call <8 x i32> @llvm.masked.load.v8i32.p0(ptr align 4
// CHECK-SAME: <8 x i1>
// CHECK-SAME: <8 x i32> poison)
v8i test_load(v8b m, int *p) {
  return __builtin_masked_load(m, p);
}

// CHECK-LABEL: @test_load_passthru(
// CHECK: call <8 x i32> @llvm.masked.load.v8i32.p0(ptr align 4
// CHECK-NOT: poison
v8i test_load_passthru(v8b m, int *p, v8i t) {
  return __builtin_masked_load(m, p, t);
}

// CHECK-LABEL: @test_load_expand(
// CHECK: call <8 x i32> @llvm.masked.expandload.v8i32(ptr
// CHECK-SAME: <8 x i1>
// CHECK-SAME: <8 x i32>
v8i test_load_expand(v8b m, int *p, v8i t) {
  return __builtin_masked_expand_load(m, p, t);
}

// CHECK-LABEL: @test_store(
// CHECK: call void @llvm.masked.store.v8i32.p0(<8 x i32>
// CHECK-SAME: ptr align 4
// CHECK-SAME: <8 x i1>
void test_store(v8b m, v8i v, int *p) {
  __builtin_masked_store(m, v, p);
}

// CHECK-LABEL: @test_compress_store(
// CHECK: call void @llvm.masked.compressstore.v8i32(<8 x i32>
// CHECK-SAME: ptr
// CHECK-SAME: <8 x i1>
void test_compress_store(v8b m, v8i v, int *p) {
  __builtin_masked_compress_store(m, v, p);
}

// CHECK-LABEL: @test_gather(
// CHECK: getelementptr i32, ptr %{{.*}}, <8 x i32> %{{.*}}
// CHECK: call <8 x i32> @llvm.masked.gather.v8i32.v8p0(<8 x ptr> align 4
// CHECK-SAME: <8 x i1>
// CHECK-SAME: <8 x i32> poison)
v8i test_gather(v8b mask, v8i idx, int *ptr) {
  return __builtin_masked_gather(mask, idx, ptr);
}

// CHECK-LABEL: @test_scatter(
// CHECK: getelementptr i32, ptr %{{.*}}, <8 x i32> %{{.*}}
// CHECK: call void @llvm.masked.scatter.v8i32.v8p0(<8 x i32>
// CHECK-SAME: <8 x ptr> align 4
// CHECK-SAME: <8 x i1>
void test_scatter(v8b mask, v8i val, v8i idx, int *ptr) {
  __builtin_masked_scatter(mask, val, idx, ptr);
}

// Address space variants
// CHECK-LABEL: @test_load_as(
// CHECK: call <8 x i32> @llvm.masked.load.v8i32.p42(ptr addrspace(42) align 4
v8i test_load_as(v8b mask, int __attribute__((address_space(42))) * ptr) {
  return __builtin_masked_load(mask, ptr);
}

// CHECK-LABEL: @test_gather_as(
// CHECK: getelementptr i32, ptr addrspace(42) %{{.*}}, <8 x i32> %{{.*}}
// CHECK: call <8 x i32> @llvm.masked.gather.v8i32.v8p42(<8 x ptr addrspace(42)> align 4
v8i test_gather_as(v8b mask, v8i idx, int __attribute__((address_space(42))) *ptr) {
  return __builtin_masked_gather(mask, idx, ptr);
}

// CHECK-LABEL: @test_scatter_as(
// CHECK: getelementptr i32, ptr addrspace(42) %{{.*}}, <8 x i32> %{{.*}}
// CHECK: call void @llvm.masked.scatter.v8i32.v8p42(<8 x i32>
// CHECK-SAME: <8 x ptr addrspace(42)> align 4
void test_scatter_as(v8b mask, v8i val, v8i idx, int __attribute__((address_space(42))) *ptr) {
  __builtin_masked_scatter(mask, val, idx, ptr);
}
