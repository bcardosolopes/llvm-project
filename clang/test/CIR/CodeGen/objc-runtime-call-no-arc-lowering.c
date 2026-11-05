// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// In plain C mode, objc_* symbols are ordinary function names and must not be
// rewritten to ARC intrinsics.
extern void objc_release(void *);
extern void *objc_retain(void *);
extern void objc_storeStrong(void **, void *);
extern void *objc_autoreleaseReturnValue(void *);
extern void *objc_retainAutoreleasedReturnValue(void *);
extern void *objc_autorelease(void *);
extern void *objc_retainAutorelease(void *);
extern void *objc_retainBlock(void *);
extern void objc_copyWeak(void **, void **);
extern void objc_moveWeak(void **, void **);
extern void *objc_loadWeak(void **);
extern void *objc_loadWeakRetained(void **);
extern void *objc_initWeak(void **, void *);
extern void objc_destroyWeak(void **);

void test_objc_release_plain_c(void *p) {
  objc_release(p);
}

// CIR-LABEL: @test_objc_release_plain_c
// CIR: call void @objc_release(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_release_plain_c
// OGCG: call void @objc_release(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_retain_plain_c(void *p) {
  return objc_retain(p);
}

// CIR-LABEL: @test_objc_retain_plain_c
// CIR: call ptr @objc_retain(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_retain_plain_c
// OGCG: call ptr @objc_retain(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void test_objc_storeStrong_plain_c(void **slot, void *v) {
  objc_storeStrong(slot, v);
}

// CIR-LABEL: @test_objc_storeStrong_plain_c
// CIR: call void @objc_storeStrong(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_storeStrong_plain_c
// OGCG: call void @objc_storeStrong(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_autoreleaseReturnValue_plain_c(void *p) {
  return objc_autoreleaseReturnValue(p);
}

// CIR-LABEL: @test_objc_autoreleaseReturnValue_plain_c
// CIR: call ptr @objc_autoreleaseReturnValue(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_autoreleaseReturnValue_plain_c
// OGCG: call ptr @objc_autoreleaseReturnValue(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_retainAutoreleasedReturnValue_plain_c(void *p) {
  return objc_retainAutoreleasedReturnValue(p);
}

// CIR-LABEL: @test_objc_retainAutoreleasedReturnValue_plain_c
// CIR: call ptr @objc_retainAutoreleasedReturnValue(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_retainAutoreleasedReturnValue_plain_c
// OGCG: call ptr @objc_retainAutoreleasedReturnValue(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_autorelease_plain_c(void *p) {
  return objc_autorelease(p);
}

// CIR-LABEL: @test_objc_autorelease_plain_c
// CIR: call ptr @objc_autorelease(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_autorelease_plain_c
// OGCG: call ptr @objc_autorelease(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_retainAutorelease_plain_c(void *p) {
  return objc_retainAutorelease(p);
}

// CIR-LABEL: @test_objc_retainAutorelease_plain_c
// CIR: call ptr @objc_retainAutorelease(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_retainAutorelease_plain_c
// OGCG: call ptr @objc_retainAutorelease(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_retainBlock_plain_c(void *p) {
  return objc_retainBlock(p);
}

// CIR-LABEL: @test_objc_retainBlock_plain_c
// CIR: call ptr @objc_retainBlock(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_retainBlock_plain_c
// OGCG: call ptr @objc_retainBlock(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void test_objc_copyWeak_plain_c(void **dst, void **src) {
  objc_copyWeak(dst, src);
}

// CIR-LABEL: @test_objc_copyWeak_plain_c
// CIR: call void @objc_copyWeak(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_copyWeak_plain_c
// OGCG: call void @objc_copyWeak(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void test_objc_moveWeak_plain_c(void **dst, void **src) {
  objc_moveWeak(dst, src);
}

// CIR-LABEL: @test_objc_moveWeak_plain_c
// CIR: call void @objc_moveWeak(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_moveWeak_plain_c
// OGCG: call void @objc_moveWeak(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_loadWeak_plain_c(void **src) {
  return objc_loadWeak(src);
}

// CIR-LABEL: @test_objc_loadWeak_plain_c
// CIR: call ptr @objc_loadWeak(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_loadWeak_plain_c
// OGCG: call ptr @objc_loadWeak(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_loadWeakRetained_plain_c(void **src) {
  return objc_loadWeakRetained(src);
}

// CIR-LABEL: @test_objc_loadWeakRetained_plain_c
// CIR: call ptr @objc_loadWeakRetained(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_loadWeakRetained_plain_c
// OGCG: call ptr @objc_loadWeakRetained(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void *test_objc_initWeak_plain_c(void **dst, void *src) {
  return objc_initWeak(dst, src);
}

// CIR-LABEL: @test_objc_initWeak_plain_c
// CIR: call ptr @objc_initWeak(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_initWeak_plain_c
// OGCG: call ptr @objc_initWeak(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.

void test_objc_destroyWeak_plain_c(void **dst) {
  objc_destroyWeak(dst);
}

// CIR-LABEL: @test_objc_destroyWeak_plain_c
// CIR: call void @objc_destroyWeak(ptr noundef %{{.*}})
// CIR-NOT: @llvm.objc.

// OGCG-LABEL: @test_objc_destroyWeak_plain_c
// OGCG: call void @objc_destroyWeak(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.objc.
