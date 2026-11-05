// RUN: %clang_cc1 -std=c++14 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -ftrivial-auto-var-init=pattern -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++14 -triple x86_64-unknown-linux-gnu \
// RUN:   -ftrivial-auto-var-init=pattern -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Regression test: direct-CIR used to pre-initialize scalar locals
// even when a non-constant initializer fully overwrote the storage. Match
// upstream CodeGen behavior:
// scalar brace/custom initializers should not get a redundant auto-init, while
// self-referential scalar initializers must still keep the seeded store.

#pragma clang diagnostic ignored "-Wuninitialized"

template <typename T> void sink(T &) noexcept;

enum SmallEnum { Small = 42 };

extern "C" void test_int_braces() {
  int x = {};
  sink(x);
}

// LLVM-LABEL: @test_int_braces(
// LLVM: %[[X:.*]] = alloca i32{{.*}}align 4
// LLVM-NEXT: store i32 0, ptr %[[X]], align 4
// LLVM-NOT: !annotation
// LLVM-NEXT: call void @{{.*}}sink{{.*}}(ptr noundef nonnull align 4 dereferenceable(4) %[[X]])
//
// OGCG-LABEL: @test_int_braces(
// OGCG: %[[X:.*]] = alloca i32{{.*}}align 4
// OGCG-NEXT: store i32 0, ptr %[[X]], align 4
// OGCG-NOT: !annotation
// OGCG-NEXT: call void @{{.*}}sink{{.*}}(ptr noundef nonnull align 4 dereferenceable(4) %[[X]])

extern "C" void test_bool_braces() {
  bool x = {};
  sink(x);
}

// LLVM-LABEL: @test_bool_braces(
// LLVM: %[[X:.*]] = alloca i8{{.*}}align 1
// LLVM-NEXT: store i8 0, ptr %[[X]], align 1
// LLVM-NOT: !annotation
// LLVM-NEXT: call void @{{.*}}sink{{.*}}(ptr noundef nonnull align 1 dereferenceable(1) %[[X]])
//
// OGCG-LABEL: @test_bool_braces(
// OGCG: %[[X:.*]] = alloca i8{{.*}}align 1
// OGCG-NEXT: store i8 0, ptr %[[X]], align 1
// OGCG-NOT: !annotation
// OGCG-NEXT: call void @{{.*}}sink{{.*}}(ptr noundef nonnull align 1 dereferenceable(1) %[[X]])

extern "C" void test_enum_custom() {
  SmallEnum x = (SmallEnum)42;
  sink(x);
}

// LLVM-LABEL: @test_enum_custom(
// LLVM: %[[X:.*]] = alloca i32{{.*}}align 4
// LLVM-NEXT: store i32 42, ptr %[[X]], align 4
// LLVM-NOT: !annotation
// LLVM-NEXT: call void @{{.*}}sink{{.*}}(ptr noundef nonnull align 4 dereferenceable(4) %[[X]])
//
// OGCG-LABEL: @test_enum_custom(
// OGCG: %[[X:.*]] = alloca i32{{.*}}align 4
// OGCG-NEXT: store i32 42, ptr %[[X]], align 4
// OGCG-NOT: !annotation
// OGCG-NEXT: call void @{{.*}}sink{{.*}}(ptr noundef nonnull align 4 dereferenceable(4) %[[X]])

extern "C" int test_self_ref() {
  int x = x;
  return x;
}

// LLVM-LABEL: @test_self_ref(
// LLVM: %[[X:.*]] = alloca i32{{.*}}align 4
// LLVM-NEXT: store i32 -1431655766, ptr %[[X]], align 4, !annotation [[LLVM_AUTO_INIT:![0-9]+]]
// LLVM-NEXT: %[[LOAD:.*]] = load i32, ptr %[[X]], align 4
// LLVM-NEXT: store i32 %[[LOAD]], ptr %[[X]], align 4
// LLVM-NEXT: %[[RET:.*]] = load i32, ptr %[[X]], align 4
// LLVM-NEXT: ret i32 %[[RET]]
// LLVM: [[LLVM_AUTO_INIT]] = !{!"auto-init"}
//
// OGCG-LABEL: @test_self_ref(
// OGCG: %[[X:.*]] = alloca i32{{.*}}align 4
// OGCG-NEXT: store i32 -1431655766, ptr %[[X]], align 4, !annotation [[OGCG_AUTO_INIT:![0-9]+]]
// OGCG-NEXT: %[[LOAD:.*]] = load i32, ptr %[[X]], align 4
// OGCG-NEXT: store i32 %[[LOAD]], ptr %[[X]], align 4
// OGCG-NEXT: %[[RET:.*]] = load i32, ptr %[[X]], align 4
// OGCG-NEXT: ret i32 %[[RET]]
// OGCG: [[OGCG_AUTO_INIT]] = !{!"auto-init"}
