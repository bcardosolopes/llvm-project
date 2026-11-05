// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s

struct Pair {
  int x;
  int y;
};

extern void sink(int);

void test(Pair p) {
  ^{ sink(p.x + p.y); }();
}

// CHECK-LABEL: define internal void @{{.*}}block_invoke.invoke(
// CHECK: getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, %struct.Pair }, ptr %{{.*}}, i32 0, i32 5
// CHECK: getelementptr inbounds nuw %struct.Pair, ptr %{{.*}}, i32 0, i32 0
// CHECK: load i32, ptr %{{.*}}, align 8
// CHECK: getelementptr inbounds nuw %struct.Pair, ptr %{{.*}}, i32 0, i32 1
// CHECK: load i32, ptr %{{.*}}, align 4
// CHECK: add nsw i32 %{{.*}}, %{{.*}}
// CHECK: call void @_Z4sinki(i32 noundef %{{.*}})

// CHECK-LABEL: define void @_Z4test4Pair(
// CHECK: %[[CAPTURE_SLOT:[0-9]+]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, %struct.Pair }, ptr %{{.*}}, i32 0, i32 5
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %[[CAPTURE_SLOT]], ptr align 4 %{{.*}}, i64 8, i1 false)
