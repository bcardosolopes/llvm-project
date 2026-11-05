// RUN: %clang_cc1 %s -fblocks -triple x86_64-apple-darwin -fclangir -emit-llvm -o - | FileCheck %s
// RUN: %clang_cc1 %s -fblocks -triple arm64-apple-darwin -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=ARM64

// Test that block captures of C++ objects with copy constructors that have
// default arguments correctly emit all arguments and clean up temporaries.
// This is a CIR version of clang/test/CodeGenCXX/copy-in-cplus-object.cpp.

struct S {
  S(const char *);
  ~S();
};

struct TestObject {
    TestObject(const TestObject& inObj, int def = 100,  const S &Silly = "silly");
    TestObject();
    ~TestObject();
    TestObject& operator=(const TestObject& inObj);
    int version() const;
};

void testRoutine() {
    TestObject one;
    int (^V)() = ^{ return one.version(); };
}

// The copy helper must call the copy ctor with all args (including default
// args) and destroy the S temporary afterward.
// CHECK: @___Z11testRoutinev_block_invoke.descriptor = internal constant {{.*}}ptr @__copy_helper_block_8_32c16_ZTS10TestObject, ptr @__destroy_helper_block_8_32c16_ZTS10TestObject{{.*}}
// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32c16_ZTS10TestObject(
// CHECK-SAME: unnamed_addr #[[HELPER_ATTRS:[0-9]+]]
// CHECK: call void @_ZN1SC1EPKc
// CHECK: call void @_ZN10TestObjectC1ERKS_iRK1S
// CHECK: call void @_ZN1SD1Ev
// CHECK: ret void
// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32c16_ZTS10TestObject(
// CHECK-SAME: unnamed_addr #[[HELPER_ATTRS]]

// testRoutine does the direct block capture with default args.
// CHECK-LABEL: define {{.*}} @_Z11testRoutinev
// CHECK: call void @_ZN10TestObjectC1Ev
// CHECK: call void @_ZN1SC1EPKc
// CHECK: call void @_ZN10TestObjectC1ERKS_iRK1S
// CHECK: call void @_ZN1SD1Ev
// CHECK: call void @_ZN10TestObjectD1Ev
// CHECK: call void @_ZN10TestObjectD1Ev
// CHECK: attributes #[[HELPER_ATTRS]] = { noinline nounwind "min-legal-vector-width"="0"

// ARM64: @___Z11testRoutinev_block_invoke.descriptor = internal constant {{.*}}ptr @__copy_helper_block_8_32c16_ZTS10TestObject, ptr @__destroy_helper_block_8_32c16_ZTS10TestObject{{.*}}
// ARM64-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32c16_ZTS10TestObject(
// ARM64-SAME: unnamed_addr #[[ARM64_HELPER_ATTRS:[0-9]+]]
// ARM64-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32c16_ZTS10TestObject(
// ARM64-SAME: unnamed_addr #[[ARM64_HELPER_ATTRS]]
// ARM64: attributes #[[ARM64_HELPER_ATTRS]] = { noinline nounwind
