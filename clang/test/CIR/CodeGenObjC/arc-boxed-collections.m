// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -fobjc-runtime-has-weak -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s

@interface NSArray @end
@interface NSArray (ArrayCreation)
+ (NSArray *)arrayWithObjects:(const id [])objects count:(unsigned long)count;
@end

@interface NSDictionary @end
@interface NSDictionary (DictionaryCreation)
+ (NSDictionary *)dictionaryWithObjects:(const id [])objects
                                forKeys:(const id [])keys
                                  count:(unsigned long)count;
@end

void test_array(id a, id b) {
  id arr = @[a, b];
  (void)arr;
}

// CHECK-LABEL: define{{.*}} void @test_array(
// CHECK: %[[OBJECTS:.*]] = alloca [2 x ptr]
// CHECK: %[[OBJ0:.*]] = getelementptr inbounds [2 x ptr], ptr %[[OBJECTS]], i32 0, i64 0
// CHECK-NEXT: store ptr %{{.*}}, ptr %[[OBJ0]]
// CHECK: %[[OBJ1:.*]] = getelementptr inbounds [2 x ptr], ptr %[[OBJECTS]], i32 0, i64 1
// CHECK-NEXT: store ptr %{{.*}}, ptr %[[OBJ1]]
// CHECK: %[[ARRAYPTR:.*]] = getelementptr inbounds nuw ptr, ptr %[[OBJECTS]], i32 0
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %[[ARRAYPTR]], i64 noundef 2) [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK: call void (...) @llvm.objc.clang.arc.use(ptr noundef %{{.*}}, ptr noundef %{{.*}})

void test_dictionary(id k1, id o1, id k2, id o2) {
  id dict = @{k1 : o1, k2 : o2};
  (void)dict;
}

// CHECK-LABEL: define{{.*}} void @test_dictionary(
// CHECK: %[[OBJECTS:.*]] = alloca [2 x ptr]
// CHECK: %[[KEYS:.*]] = alloca [2 x ptr]
// CHECK: %[[KEY0:.*]] = getelementptr inbounds [2 x ptr], ptr %[[KEYS]], i32 0, i64 0
// CHECK-NEXT: store ptr %{{.*}}, ptr %[[KEY0]]
// CHECK: %[[OBJ0:.*]] = getelementptr inbounds [2 x ptr], ptr %[[OBJECTS]], i32 0, i64 0
// CHECK-NEXT: store ptr %{{.*}}, ptr %[[OBJ0]]
// CHECK: %[[KEY1:.*]] = getelementptr inbounds [2 x ptr], ptr %[[KEYS]], i32 0, i64 1
// CHECK-NEXT: store ptr %{{.*}}, ptr %[[KEY1]]
// CHECK: %[[OBJ1:.*]] = getelementptr inbounds [2 x ptr], ptr %[[OBJECTS]], i32 0, i64 1
// CHECK-NEXT: store ptr %{{.*}}, ptr %[[OBJ1]]
// CHECK: %[[OBJECTSPTR:.*]] = getelementptr inbounds nuw ptr, ptr %[[OBJECTS]], i32 0
// CHECK: %[[KEYSPTR:.*]] = getelementptr inbounds nuw ptr, ptr %[[KEYS]], i32 0
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %[[OBJECTSPTR]], ptr noundef %[[KEYSPTR]], i64 noundef 2) [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK: call void (...) @llvm.objc.clang.arc.use(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
