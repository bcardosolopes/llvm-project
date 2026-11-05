// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O2
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O0 -disable-llvm-passes -fclangir -o - %s | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-O0

void noescapeFunc0(id, void (^__attribute__((noescape)))(void));
void escapingFunc0(void (^)(void));

struct S0 {
  id a;
  id b;
};

void test(void) {
  id a;
  __block struct S0 b0;
  noescapeFunc0(a, ^{ (void)b0; });
  escapingFunc0(^{ (void)b0; });
}

// CHECK-LABEL: define internal void @__Block_byref_object_copy_(
// CHECK: %[[DSTREC:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr, %struct.S0 }, ptr %0, i32 0, i32 7
// CHECK: %[[SRCREC:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr, %struct.S0 }, ptr %1, i32 0, i32 7
// CHECK: call void @__move_constructor_8_8_s0_s8(ptr noundef %[[DSTREC]], ptr noundef %[[SRCREC]])
// CHECK: ret void

// CHECK-LABEL: define internal void @__Block_byref_object_dispose_(
// CHECK: %[[DISPOSEREC:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr, %struct.S0 }, ptr %0, i32 0, i32 7
// CHECK: call void @__destructor_8_s0_s8(ptr noundef %[[DISPOSEREC]])
// CHECK: ret void

// CHECK-LABEL: define linkonce_odr hidden void @__move_constructor_8_8_s0_s8(
// CHECK: %[[DST0:.*]] = getelementptr inbounds nuw %struct.S0, ptr %0, i32 0, i32 0
// CHECK: %[[SRC0:.*]] = getelementptr inbounds nuw %struct.S0, ptr %1, i32 0, i32 0
// CHECK: %[[V0:.*]] = load ptr, ptr %[[SRC0]], align 8
// CHECK: store ptr null, ptr %[[SRC0]], align 8
// CHECK: store ptr %[[V0]], ptr %[[DST0]], align 8
// CHECK: %[[DST1:.*]] = getelementptr inbounds nuw %struct.S0, ptr %0, i32 0, i32 1
// CHECK: %[[SRC1:.*]] = getelementptr inbounds nuw %struct.S0, ptr %1, i32 0, i32 1
// CHECK: %[[V1:.*]] = load ptr, ptr %[[SRC1]], align 8
// CHECK: store ptr null, ptr %[[SRC1]], align 8
// CHECK: store ptr %[[V1]], ptr %[[DST1]], align 8
// CHECK: ret void

// CHECK-LABEL: define linkonce_odr hidden void @__destructor_8_s0_s8(
// CHECK: %[[DISPOSE0:.*]] = getelementptr inbounds nuw %struct.S0, ptr %0, i32 0, i32 0
// CHECK-O2: %[[LOAD0:.*]] = load ptr, ptr %[[DISPOSE0]], align 8
// CHECK-O2: call void @llvm.objc.release(ptr %[[LOAD0]])
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[DISPOSE0]], ptr null)
// CHECK: %[[DISPOSE1:.*]] = getelementptr inbounds nuw %struct.S0, ptr %0, i32 0, i32 1
// CHECK-O2: %[[LOAD1:.*]] = load ptr, ptr %[[DISPOSE1]], align 8
// CHECK-O2: call void @llvm.objc.release(ptr %[[LOAD1]])
// CHECK-O0: call void @llvm.objc.storeStrong(ptr %[[DISPOSE1]], ptr null)
// CHECK: ret void

// CHECK-LABEL: define linkonce_odr hidden void @__default_constructor_8_s0_s8(
// CHECK: %[[INIT0:.*]] = getelementptr inbounds nuw %struct.S0, ptr %0, i32 0, i32 0
// CHECK: store ptr null, ptr %[[INIT0]], align 8
// CHECK: %[[INIT1:.*]] = getelementptr inbounds nuw %struct.S0, ptr %0, i32 0, i32 1
// CHECK: store ptr null, ptr %[[INIT1]], align 8
// CHECK: ret void

// CHECK-LABEL: define{{.*}} void @test()
// CHECK: %[[A:.*]] = alloca ptr
// CHECK: %[[BYREF:.*]] = alloca { ptr, ptr, i32, i32, ptr, ptr, ptr, %struct.S0 }
// CHECK: %[[PAYLOAD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr, %struct.S0 }, ptr %[[BYREF]], i32 0, i32 7
// CHECK: %[[COPY:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr, %struct.S0 }, ptr %[[BYREF]], i32 0, i32 4
// CHECK: store ptr @__Block_byref_object_copy_, ptr %[[COPY]], align 8
// CHECK: %[[DISPOSE:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, ptr, ptr, ptr, %struct.S0 }, ptr %[[BYREF]], i32 0, i32 5
// CHECK: store ptr @__Block_byref_object_dispose_, ptr %[[DISPOSE]], align 8
// CHECK: call void @__default_constructor_8_s0_s8(ptr noundef %[[PAYLOAD]])
// CHECK: call void @escapingFunc0
// CHECK: call void @_Block_object_dispose(ptr %[[BYREF]], i32 8)
// CHECK: call void @__destructor_8_s0_s8(ptr noundef
