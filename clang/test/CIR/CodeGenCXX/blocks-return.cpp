// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM

int run_global(int (^)(void));
int run_capture(int (^)(void));

int test_global() {
  return run_global(^{ return 1; });
}

int test_capture(int x) {
  return run_capture(^{ return x + 1; });
}

// LLVM-DAG: @___Z11test_globalv_block_invoke.literal = internal constant { ptr, i32, i32, ptr, ptr }
// LLVM-DAG: @___Z12test_capturei_block_invoke.descriptor = internal constant { i64, i64, ptr, ptr } { i64 0, i64 36,
// LLVM-LABEL: define internal noundef i32 @___Z11test_globalv_block_invoke.invoke(ptr noundef %{{.*}})
// LLVM: ret i32 1
// LLVM-LABEL: define dso_local noundef i32 @_Z11test_globalv()
// LLVM: %[[GLOBALRET:.*]] = call noundef i32 @_Z10run_globalU13block_pointerFivE(ptr noundef @___Z11test_globalv_block_invoke.literal)
// LLVM: ret i32 %[[GLOBALRET]]

// LLVM-LABEL: define internal noundef i32 @___Z12test_capturei_block_invoke.invoke(ptr noundef %{{.*}})
// LLVM: %[[CAPADDR:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, i32 }>, ptr %{{.*}}, i32 0, i32 5
// LLVM: %[[CAPLOAD:.*]] = load i32, ptr %[[CAPADDR]], align 8
// LLVM: %[[ADD:.*]] = add nsw i32 %[[CAPLOAD]], 1
// LLVM-NEXT: ret i32 %[[ADD]]
// LLVM-LABEL: define dso_local noundef i32 @_Z12test_capturei(i32 noundef %{{.*}})
// LLVM: %[[BLOCK:.*]] = alloca <{ ptr, i32, i32, ptr, ptr, i32 }>, i64 1, align 8
// LLVM: store ptr @_NSConcreteStackBlock, ptr %{{.*}}, align 8
// LLVM: store ptr @___Z12test_capturei_block_invoke.invoke, ptr %{{.*}}, align 8
// LLVM: %[[CAPSLOT:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, i32 }>, ptr %[[BLOCK]], i32 0, i32 5
// LLVM: %[[XLOAD:.*]] = load i32, ptr %{{.*}}, align 4
// LLVM: store i32 %[[XLOAD]], ptr %[[CAPSLOT]], align 8
// LLVM: %[[CAPRET:.*]] = call noundef i32 @_Z11run_captureU13block_pointerFivE(ptr noundef %[[BLOCK]])
// LLVM: store i32 %[[CAPRET]], ptr %{{.*}}, align 4
// LLVM: ret i32 %{{.*}}
