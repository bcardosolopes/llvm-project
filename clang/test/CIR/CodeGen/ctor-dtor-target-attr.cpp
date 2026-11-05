// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that constructors and destructors with target attributes don't
// crash in constructAttributeList / setCIRFunctionAttributesForDefinition
// when constructing GlobalDecl from CXXConstructorDecl/CXXDestructorDecl.
// Also verify target features are correctly propagated.

struct CtorTarget {
  __attribute__((target("avx512f"))) CtorTarget() {}
  ~CtorTarget() {}
};

void use() { CtorTarget c; }

// The constructor should have avx512f in target-features.
// CHECK: define{{.*}} void @_ZN10CtorTargetC2Ev(
// CHECK-SAME: #[[CTOR_ATTRS:[0-9]+]]

// The destructor should NOT have avx512f.
// CHECK: define{{.*}} void @_ZN10CtorTargetD2Ev(
// CHECK-SAME: #[[DTOR_ATTRS:[0-9]+]]

// CHECK: attributes #[[CTOR_ATTRS]] = {{{.*}}"target-features"="{{[^"]*}}+avx512f{{[^"]*}}"
// CHECK-NOT: attributes #[[DTOR_ATTRS]] = {{{.*}}+avx512f
