// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -fblocks -emit-llvm %s  -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -fblocks -disable-llvm-passes -fclangir -emit-llvm %s  -o - | FileCheck %s --check-prefix=CIR %}
// RUN: %clang_cc1 -triple arm64e-apple-ios -fptrauth-calls -fptrauth-intrinsics -fptrauth-block-descriptor-pointers -fblocks -emit-llvm %s  -o - | FileCheck %s --check-prefix=OGCGE
// RUN: %if cir-support %{ %clang_cc1 -triple arm64e-apple-ios -fptrauth-calls -fptrauth-intrinsics -fptrauth-block-descriptor-pointers -fblocks -disable-llvm-passes -fclangir -emit-llvm %s  -o - | FileCheck %s --check-prefix=CIRE %}

struct A {
  int value;
};
struct A *createA(void);

void use_block(int (^)(void));

// CHECK: @__block_descriptor_tmp = internal constant { i64, i64, ptr, ptr } { i64 0, i64 32, ptr @.str, ptr null }
// CHECK: @__block_literal_global = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr ptrauth (ptr @__test_global_block_literal_block_invoke, i32 0, i64 0, ptr getelementptr inbounds ({ ptr, i32, i32, ptr, ptr }, ptr @__block_literal_global, i32 0, i32 3)), ptr @__block_descriptor_tmp }
// CIR: @__test_global_block_literal_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr } { i64 0, i64 32, ptr @__test_global_block_literal_block_invoke.sig, ptr null }
// CIR: @__test_global_block_literal_block_invoke.literal = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr ptrauth (ptr @__test_global_block_literal_block_invoke.invoke, i32 0, i64 0, ptr getelementptr inbounds ({ ptr, i32, i32, ptr, ptr }, ptr @__test_global_block_literal_block_invoke.literal, i32 0, i32 3)), ptr @__test_global_block_literal_block_invoke.descriptor }
// OGCGE: @__block_descriptor_tmp = internal constant { i64, i64, ptr, ptr } { i64 0, i64 32, ptr @.str, ptr null }
// OGCGE: @__block_literal_global = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr ptrauth (ptr @__test_global_block_literal_block_invoke, i32 0, i64 0, ptr getelementptr inbounds ({ ptr, i32, i32, ptr, ptr }, ptr @__block_literal_global, i32 0, i32 3)), ptr ptrauth (ptr @__block_descriptor_tmp, i32 2, i64 49339, ptr getelementptr inbounds ({ ptr, i32, i32, ptr, ptr }, ptr @__block_literal_global, i32 0, i32 4)) }
// CIRE: @__test_global_block_literal_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr } { i64 0, i64 32, ptr @__test_global_block_literal_block_invoke.sig, ptr null }
// CIRE: @__test_global_block_literal_block_invoke.literal = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr ptrauth (ptr @__test_global_block_literal_block_invoke.invoke, i32 0, i64 0, ptr getelementptr inbounds ({ ptr, i32, i32, ptr, ptr }, ptr @__test_global_block_literal_block_invoke.literal, i32 0, i32 3)), ptr ptrauth (ptr @__test_global_block_literal_block_invoke.descriptor, i32 2, i64 49339, ptr getelementptr inbounds ({ ptr, i32, i32, ptr, ptr }, ptr @__test_global_block_literal_block_invoke.literal, i32 0, i32 4)) }

void test_global_block_literal(void) {
  use_block(^{ return 7; });
}

// CIR: @__test_block_address_capture_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr, ptr, ptr } { i64 0, i64 40, ptr ptrauth (ptr @__copy_helper_block_8_32p1d30, i32 0, i64 0, ptr getelementptr inbounds ({ i64, i64, ptr, ptr, ptr, ptr }, ptr @__test_block_address_capture_block_invoke.descriptor, i32 0, i32 2)), ptr ptrauth (ptr @__destroy_helper_block_8_32, i32 0, i64 0, ptr getelementptr inbounds ({ i64, i64, ptr, ptr, ptr, ptr }, ptr @__test_block_address_capture_block_invoke.descriptor, i32 0, i32 3)), ptr @__test_block_address_capture_block_invoke.sig, ptr null }
// CHECK-LABEL: define void @test_block_nonaddress_capture(
// CIR-LABEL: define void @test_block_nonaddress_capture(
void test_block_nonaddress_capture() {
  // CHECK: [[VAR:%.*]] = alloca ptr,
  // CHECK: [[BLOCK:%.*]] = alloca
  //   flags - no copy/dispose required
  // CHECK: store i32 1073741824, ptr
  // CIR: store i32 1073741824, ptr
  // CIR: [[INVOKE_SLOT:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 3
  // CIR: [[INVOKE_DISC:%.*]] = ptrtoint ptr [[INVOKE_SLOT]] to i64
  // CIR: [[INVOKE_SIGN:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr @__test_block_nonaddress_capture_block_invoke.invoke to i64), i32 0, i64 [[INVOKE_DISC]])
  // CIR: [[INVOKE_PTR:%.*]] = inttoptr i64 [[INVOKE_SIGN]] to ptr
  // CIR: store ptr [[INVOKE_PTR]], ptr [[INVOKE_SLOT]], align 8
  // CHECK: [[CAPTURE:%.*]] = getelementptr inbounds {{.*}} [[BLOCK]], i32 0, i32 5
  // CHECK: [[LOAD:%.*]] = load ptr, ptr [[VAR]],
  // CHECK: store ptr [[LOAD]], ptr [[CAPTURE]]
  struct A * __ptrauth(1, 0, 15) ptr = createA();
  use_block(^{ return ptr->value; });
}
// CHECK-LABEL: define internal i32 @__test_block_nonaddress_capture_block_invoke
// CHECK: call i64 @llvm.ptrauth.auth(i64 {{%.*}}, i32 1, i64 15)

// CHECK-LABEL: define void @test_block_address_capture(
void test_block_address_capture() {
  // CHECK: [[VAR:%.*]] = alloca ptr,
  // CHECK: [[BLOCK:%.*]] = alloca
  //   flags - copy/dispose required
  // CHECK: store i32 1107296256, ptr
  // CHECK: [[CAPTURE:%.*]] = getelementptr inbounds {{.*}} [[BLOCK]], i32 0, i32 5
  // CHECK: [[LOAD:%.*]] = load ptr, ptr [[VAR]],
  // CHECK: [[T0:%.*]] = ptrtoint ptr [[VAR]] to i64
  // CHECK: [[OLDDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[T0]], i64 30)
  // CHECK: [[T0:%.*]] = ptrtoint ptr [[CAPTURE]] to i64
  // CHECK: [[NEWDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[T0]], i64 30)
  // CHECK: [[T0:%.*]] = icmp ne ptr [[LOAD]], null
  // CHECK: br i1 [[T0]]
  // CHECK: [[T0:%.*]] = ptrtoint ptr [[LOAD]] to i64
  // CHECK: [[T1:%.*]] = call i64 @llvm.ptrauth.resign(i64 [[T0]], i32 1, i64 [[OLDDISC]], i32 1, i64 [[NEWDISC]])
  // CHECK: [[T2:%.*]] = inttoptr i64 [[T1]] to ptr
  // CHECK: [[T0:%.*]] = phi
  // CHECK: store ptr [[T0]], ptr [[CAPTURE]]
  struct A * __ptrauth(1, 1, 30) ptr = createA();
  use_block(^{ return ptr->value; });
}
// CHECK-LABEL: define internal i32 @__test_block_address_capture_block_invoke
// CHECK: call i64 @llvm.ptrauth.auth(i64 {{%.*}}, i32 1, i64 {{%.*}})
// CIR-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32p1d30(
// CIR: [[NEWSLOT:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 5
// CIR: [[OLDSLOT:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 5
// CIR: [[LOAD:%.*]] = load ptr, ptr [[OLDSLOT]], align 8
// CIR: [[OLDINT:%.*]] = ptrtoint ptr [[OLDSLOT]] to i64
// CIR: [[OLDDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[OLDINT]], i64 30)
// CIR: [[NEWINT:%.*]] = ptrtoint ptr [[NEWSLOT]] to i64
// CIR: [[NEWDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[NEWINT]], i64 30)
// CIR: call i64 @llvm.ptrauth.resign(i64 {{.*}}, i32 1, i64 [[OLDDISC]], i32 1, i64 [[NEWDISC]])
// CIR-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32(
// CIR: ret void
// CIR-LABEL: define void @test_block_address_capture(
// CIR: store i32 1107296256, ptr
// CIR: [[INVOKE_SLOT:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 3
// CIR: [[INVOKE_DISC:%.*]] = ptrtoint ptr [[INVOKE_SLOT]] to i64
// CIR: [[INVOKE_SIGN:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr @__test_block_address_capture_block_invoke.invoke to i64), i32 0, i64 [[INVOKE_DISC]])
// CIR: [[INVOKE_PTR:%.*]] = inttoptr i64 [[INVOKE_SIGN]] to ptr
// CIR: store ptr [[INVOKE_PTR]], ptr [[INVOKE_SLOT]], align 8

// CHECK: linkonce_odr hidden void @__copy_helper_block_8_32p1d30(
// CHECK: [[OLDSLOT:%.*]] = getelementptr inbounds {{.*}} {{.*}}, i32 0, i32 5
// CHECK: [[NEWSLOT:%.*]] = getelementptr inbounds {{.*}} {{.*}}, i32 0, i32 5
// CHECK: [[LOAD:%.*]] = load ptr, ptr [[OLDSLOT]],
// CHECK: [[T0:%.*]] = ptrtoint ptr [[OLDSLOT]] to i64
// CHECK: [[OLDDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[T0]], i64 30)
// CHECK: [[T0:%.*]] = ptrtoint ptr [[NEWSLOT]] to i64
// CHECK: [[NEWDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[T0]], i64 30)
// CHECK: [[T0:%.*]] = icmp ne ptr [[LOAD]], null
// CHECK: br i1 [[T0]]
// CHECK: [[T0:%.*]] = ptrtoint ptr [[LOAD]] to i64
// CHECK: [[T1:%.*]] = call i64 @llvm.ptrauth.resign(i64 [[T0]], i32 1, i64 [[OLDDISC]], i32 1, i64 [[NEWDISC]])
// CHECK: [[T2:%.*]] = inttoptr i64 [[T1]] to ptr
// CHECK: [[T0:%.*]] = phi
// CHECK: store ptr [[T0]], ptr [[NEWSLOT]]

// CHECK-LABEL: define void @test_block_nonaddress_byref_capture(
// CIR-LABEL: define void @test_block_nonaddress_byref_capture(
void test_block_nonaddress_byref_capture() {
  //   flags - no copy/dispose required for byref
  // CHECK: store i32 0,
  // CHECK: call ptr @createA()
  //   flags - copy/dispose required for block (because it captures byref)
  // CHECK: store i32 1107296256,
  // CIR: store i32 1107296256,
  __block struct A * __ptrauth(1, 0, 45) ptr = createA();
  use_block(^{ return ptr->value; });
}

// CIR-LABEL: define internal void @__Block_byref_object_copy_
// CIR: [[NEWSLOT:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 6
// CIR: [[OLDSLOT:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 6
// CIR: [[LOAD:%.*]] = load ptr, ptr [[OLDSLOT]], align 8
// CIR: [[OLDINT:%.*]] = ptrtoint ptr [[OLDSLOT]] to i64
// CIR: [[OLDDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[OLDINT]], i64 60)
// CIR: [[NEWINT:%.*]] = ptrtoint ptr [[NEWSLOT]] to i64
// CIR: [[NEWDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[NEWINT]], i64 60)
// CIR: call i64 @llvm.ptrauth.resign(i64 {{.*}}, i32 1, i64 [[OLDDISC]], i32 1, i64 [[NEWDISC]])
// CIR-LABEL: define internal void @__Block_byref_object_dispose_(
// CIR: ret void

// CHECK-LABEL: define void @test_block_address_byref_capture(
// CIR-LABEL: define void @test_block_address_byref_capture(
void test_block_address_byref_capture() {
  // CHECK: [[BYREF:%.*]] = alloca [[BYREF_T:.*]], align
  // CHECK: [[BLOCK:%.*]] = alloca
  //   flags - byref requires copy/dispose
  // CHECK: store i32 33554432,
  // CHECK: store i32 48,
  // CHECK: [[COPY_HELPER_FIELD:%.*]] = getelementptr inbounds nuw [[BYREF_T]], ptr [[BYREF]], i32 0, i32 4
  // CHECK: [[T0:%.*]] = ptrtoint ptr [[COPY_HELPER_FIELD]] to i64
  // CHECK: [[T1:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr @__Block_byref_object_copy_ to i64), i32 0, i64 [[T0]])
  // CHECK: [[T2:%.*]] = inttoptr i64 [[T1]] to ptr
  // CHECK: store ptr [[T2]], ptr [[COPY_HELPER_FIELD]], align
  // CHECK: [[DISPOSE_HELPER_FIELD:%.*]] = getelementptr inbounds nuw [[BYREF_T]], ptr [[BYREF]], i32 0, i32 5
  // CHECK: [[T0:%.*]] = ptrtoint ptr [[DISPOSE_HELPER_FIELD]] to i64
  // CHECK: [[T1:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr @__Block_byref_object_dispose_ to i64), i32 0, i64 [[T0]])
  // CHECK: [[T2:%.*]] = inttoptr i64 [[T1]] to ptr
  // CHECK: store ptr [[T2]], ptr [[DISPOSE_HELPER_FIELD]], align
  //   flags - copy/dispose required
  // CHECK: store i32 1107296256, ptr
  // CIR: store i32 33554432,
  // CIR: [[COPY_HELPER_FIELD:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 4
  // CIR: [[COPY_HELPER_DISC:%.*]] = ptrtoint ptr [[COPY_HELPER_FIELD]] to i64
  // CIR: call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr @__Block_byref_object_copy_ to i64), i32 0, i64 [[COPY_HELPER_DISC]])
  // CIR: [[DISPOSE_HELPER_FIELD:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 5
  // CIR: [[DISPOSE_HELPER_DISC:%.*]] = ptrtoint ptr [[DISPOSE_HELPER_FIELD]] to i64
  // CIR: call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr @__Block_byref_object_dispose_ to i64), i32 0, i64 [[DISPOSE_HELPER_DISC]])
  // CIR: store i32 1107296256, ptr
  // CIR: [[INVOKE_SLOT:%.*]] = getelementptr inbounds nuw {{.*}}, ptr {{.*}}, i32 0, i32 3
  // CIR: [[INVOKE_DISC:%.*]] = ptrtoint ptr [[INVOKE_SLOT]] to i64
  // CIR: [[INVOKE_SIGN:%.*]] = call i64 @llvm.ptrauth.sign(i64 ptrtoint (ptr @__test_block_address_byref_capture_block_invoke.invoke to i64), i32 0, i64 [[INVOKE_DISC]])
  // CIR: [[INVOKE_PTR:%.*]] = inttoptr i64 [[INVOKE_SIGN]] to ptr
  // CIR: store ptr [[INVOKE_PTR]], ptr [[INVOKE_SLOT]], align 8
  __block struct A * __ptrauth(1, 1, 60) ptr = createA();
  use_block(^{ return ptr->value; });
}
// CHECK-LABEL: define internal void @__Block_byref_object_copy_
// CHECK: [[NEWSLOT:%.*]] = getelementptr inbounds {{.*}} {{.*}}, i32 0, i32 6
// CHECK: [[OLDSLOT:%.*]] = getelementptr inbounds {{.*}} {{.*}}, i32 0, i32 6
// CHECK: [[LOAD:%.*]] = load ptr, ptr [[OLDSLOT]],
// CHECK: [[T0:%.*]] = ptrtoint ptr [[OLDSLOT]] to i64
// CHECK: [[OLDDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[T0]], i64 60)
// CHECK: [[T0:%.*]] = ptrtoint ptr [[NEWSLOT]] to i64
// CHECK: [[NEWDISC:%.*]] = call i64 @llvm.ptrauth.blend(i64 [[T0]], i64 60)
// CHECK: [[T0:%.*]] = icmp ne ptr [[LOAD]], null
// CHECK: br i1 [[T0]]
// CHECK: [[T0:%.*]] = ptrtoint ptr [[LOAD]] to i64
// CHECK: [[T1:%.*]] = call i64 @llvm.ptrauth.resign(i64 [[T0]], i32 1, i64 [[OLDDISC]], i32 1, i64 [[NEWDISC]])
// CHECK: [[T2:%.*]] = inttoptr i64 [[T1]] to ptr
// CHECK: [[T0:%.*]] = phi
// CHECK: store ptr [[T0]], ptr [[NEWSLOT]]
