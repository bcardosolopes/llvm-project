// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -fobjc-dispatch-method=mixed -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-runtime-has-weak -fobjc-dispatch-method=mixed -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=OGCG

typedef struct { int x[4]; } Big;

@interface NSObject
- (id)new;
@end

@interface MyObject : NSObject
- (_Complex float)asComplexWithArg:(id)__attribute__((ns_consumed))object;
@end

@interface P
- (Big)foo;
@end

MyObject *x;
P *p;

void test_complex(void) {
  id obj = [NSObject new];
  __weak id weakObj = obj;
  _Complex float result = [x asComplexWithArg:obj];
}

void test_agg(void) {
  Big b = [p foo];
}

// CIR-LABEL: define void @test_complex()
// CIR: [[RECV:%.*]] = load ptr, ptr @x
// CIR: [[ARG_RETAIN:%.*]] = call ptr @llvm.objc.retain(ptr %{{.*}})
// CIR: [[ISNIL:%.*]] = icmp eq ptr [[RECV]], null
// CIR: br i1 [[ISNIL]], label %[[NULLBB:[^,]+]], label %[[CALLBB:[^,]+]]
// CIR: [[NULLBB]]:
// CIR: call void @llvm.objc.release(ptr [[ARG_RETAIN]]){{.*}}!clang.imprecise_release ![[CIR_EMPTYMD:[0-9]+]]
// CIR: br label %[[CONTBB:[^,]+]]
// CIR: [[CALLBB]]:
// CIR: [[MSGSEND:%.*]] = call <2 x float> @objc_msgSend(ptr noundef [[RECV]], ptr noundef {{.*}}, ptr noundef [[ARG_RETAIN]])
// CIR: br label %[[CONTBB]]
// CIR: [[CONTBB]]:
// CIR: phi { float, float } [ {{.*}}, %[[CALLBB]] ], [ zeroinitializer, %[[NULLBB]] ]
// CIR: call void @llvm.objc.destroyWeak
// CIR: call void @llvm.objc.storeStrong

// OGCG-LABEL: define void @test_complex()
// OGCG: [[RECV:%.*]] = load ptr, ptr @x
// OGCG: [[ARG_RETAIN:%.*]] = call ptr @llvm.objc.retain(ptr %{{.*}})
// OGCG: [[ISNIL:%.*]] = icmp eq ptr [[RECV]], null
// OGCG: br i1 [[ISNIL]], label %[[NULLBB:[^,]+]], label %[[CALLBB:[^,]+]]
// OGCG: [[CALLBB]]:
// OGCG: [[MSGSEND:%.*]] = call <2 x float> @objc_msgSend(ptr noundef [[RECV]], ptr noundef {{.*}}, ptr noundef [[ARG_RETAIN]])
// OGCG: br label %[[CONTBB:[^,]+]]
// OGCG: [[NULLBB]]:
// OGCG: call void @llvm.objc.release(ptr [[ARG_RETAIN]]){{.*}}!clang.imprecise_release ![[OGCG_EMPTYMD:[0-9]+]]
// OGCG: br label %[[CONTBB]]
// OGCG: [[CONTBB]]:
// OGCG: phi float [ {{.*}}, %[[CALLBB]] ], [ 0.000000e+00, %[[NULLBB]] ]
// OGCG: phi float [ {{.*}}, %[[CALLBB]] ], [ 0.000000e+00, %[[NULLBB]] ]
// OGCG: call void @llvm.objc.destroyWeak
// OGCG: call void @llvm.objc.storeStrong

// CIR-LABEL: define void @test_agg()
// CIR: [[RECV:%.*]] = load ptr, ptr @p
// CIR: [[SEL:%.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_.{{[0-9]+}}
// CIR-NOT: icmp eq ptr [[RECV]], null
// CIR: [[AGG:%.*]] = call { i64, i64 } @objc_msgSend(ptr noundef [[RECV]], ptr noundef [[SEL]])
// CIR: extractvalue { i64, i64 } [[AGG]], 0
// CIR: extractvalue { i64, i64 } [[AGG]], 1

// OGCG-LABEL: define void @test_agg()
// OGCG: [[RECV:%.*]] = load ptr, ptr @p
// OGCG: [[SEL:%.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_.{{[0-9]+}}
// OGCG: [[AGG:%.*]] = call { i64, i64 } @objc_msgSend(ptr noundef [[RECV]], ptr noundef [[SEL]])
// OGCG: extractvalue { i64, i64 } [[AGG]], 0
// OGCG: extractvalue { i64, i64 } [[AGG]], 1

// CIR: ![[CIR_EMPTYMD]] = !{}
// OGCG: ![[OGCG_EMPTYMD]] = !{}
