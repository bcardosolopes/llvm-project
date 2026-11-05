// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-fragile-10.5 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

id g;

void preinc(void) { ++g; }
void predec(void) { --g; }
id add(long n) { return g + n; }
id sub(long n) { return g - n; }

// LLVM-LABEL: define{{.*}} void @preinc()
// LLVM: %[[G:.*]] = load ptr, ptr @g, align 8
// LLVM: %[[INC:.*]] = getelementptr{{.*}}i8, ptr %[[G]], i64 8
// LLVM: store ptr %[[INC]], ptr @g, align 8
// LLVM: ret void

// OGCG-LABEL: define{{.*}} void @preinc()
// OGCG: %[[G:.*]] = load ptr, ptr @g, align 8
// OGCG: %[[INC:.*]] = getelementptr{{.*}}i8, ptr %[[G]], i64 8
// OGCG: store ptr %[[INC]], ptr @g, align 8
// OGCG: ret void

// LLVM-LABEL: define{{.*}} void @predec()
// LLVM: %[[G:.*]] = load ptr, ptr @g, align 8
// LLVM: %[[DEC:.*]] = getelementptr{{.*}}i8, ptr %[[G]], i64 -8
// LLVM: store ptr %[[DEC]], ptr @g, align 8
// LLVM: ret void

// OGCG-LABEL: define{{.*}} void @predec()
// OGCG: %[[G:.*]] = load ptr, ptr @g, align 8
// OGCG: %[[DEC:.*]] = getelementptr{{.*}}i8, ptr %[[G]], i64 -8
// OGCG: store ptr %[[DEC]], ptr @g, align 8
// OGCG: ret void

// LLVM-LABEL: define{{.*}} ptr @add
// LLVM: %[[G:.*]] = load ptr, ptr @g, align 8
// LLVM: %[[N:.*]] = load i64, ptr %{{.*}}, align 8
// LLVM: %[[SCALE:.*]] = mul i64 %[[N]], 8
// LLVM: %[[ADD:.*]] = getelementptr i8, ptr %[[G]], i64 %[[SCALE]]
// LLVM: ret ptr %[[ADD]]

// OGCG-LABEL: define{{.*}} ptr @add
// OGCG: %[[G:.*]] = load ptr, ptr @g, align 8
// OGCG: %[[N:.*]] = load i64, ptr %{{.*}}, align 8
// OGCG: %[[SCALE:.*]] = mul i64 %[[N]], 8
// OGCG: %[[ADD:.*]] = getelementptr i8, ptr %[[G]], i64 %[[SCALE]]
// OGCG: ret ptr %[[ADD]]

// LLVM-LABEL: define{{.*}} ptr @sub
// LLVM: %[[G:.*]] = load ptr, ptr @g, align 8
// LLVM: %[[N:.*]] = load i64, ptr %{{.*}}, align 8
// LLVM: %[[NEG:.*]] = sub i64 0, %[[N]]
// LLVM: %[[SCALE:.*]] = mul i64 %[[NEG]], 8
// LLVM: %[[SUB:.*]] = getelementptr i8, ptr %[[G]], i64 %[[SCALE]]
// LLVM: ret ptr %[[SUB]]

// OGCG-LABEL: define{{.*}} ptr @sub
// OGCG: %[[G:.*]] = load ptr, ptr @g, align 8
// OGCG: %[[N:.*]] = load i64, ptr %{{.*}}, align 8
// OGCG: %[[NEG:.*]] = sub i64 0, %[[N]]
// OGCG: %[[SCALE:.*]] = mul i64 %[[NEG]], 8
// OGCG: %[[SUB:.*]] = getelementptr i8, ptr %[[G]], i64 %[[SCALE]]
// OGCG: ret ptr %[[SUB]]
