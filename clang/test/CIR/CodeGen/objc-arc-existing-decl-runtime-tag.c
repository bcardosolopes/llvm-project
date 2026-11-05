// RUN: %clang_cc1 -triple x86_64-apple-darwin -x objective-c -fobjc-arc -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin -x objective-c -fobjc-arc -emit-llvm -disable-llvm-passes %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

typedef void *id;
extern void objc_storeStrong(id *, id);

id test_existing_arc_decl(id obj) {
  id local = obj;
  return local;
}

// CIR-LABEL: @test_existing_arc_decl
// CIR: call ptr @llvm.objc.retain(ptr %{{.*}})
// CIR: call void @llvm.objc.storeStrong(ptr %{{.*}}, ptr null)

// OGCG-LABEL: @test_existing_arc_decl
// OGCG: call ptr @llvm.objc.retain(ptr %{{.*}})
// OGCG: call void @llvm.objc.storeStrong(ptr %{{.*}}, ptr null)
