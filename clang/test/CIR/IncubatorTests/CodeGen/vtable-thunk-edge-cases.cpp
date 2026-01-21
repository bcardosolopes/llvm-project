// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test edge cases for thunk generation:
// 1. Deep inheritance hierarchies
// 2. Empty base optimization affecting offsets
// 3. Multiple overrides in diamond inheritance
// 4. Mix of polymorphic and non-polymorphic bases

// ============================================================================
// Test 1: Deep Inheritance Hierarchy
// ============================================================================

class Level0 {
public:
  virtual void method0() {}
};

class Level1 : public Level0 {
public:
  virtual void method1() {}
  int data1;
};

class Level2A : public Level1 {
public:
  virtual void method2a() {}
  int data2a;
};

class Level2B {
public:
  virtual void method2b() {}
  int data2b;
};

class DeepDerived : public Level2A, public Level2B {
public:
  void method2b() override {}
};

void testDeep() {
  DeepDerived d;
  Level2B* b = &d;
  b->method2b();  // Needs thunk due to Level2B offset
}

// ============================================================================
// Test 2: Empty Base Optimization
// ============================================================================

class EmptyBase {
public:
  virtual void emptyMethod() {}
};

class NonEmptyBase {
public:
  virtual void nonEmptyMethod() {}
  int data;
};

class EmptyDerived : public EmptyBase, public NonEmptyBase {
public:
  void nonEmptyMethod() override {}
};

void testEmpty() {
  EmptyDerived d;
  NonEmptyBase* b = &d;
  b->nonEmptyMethod();
}

// ============================================================================
// Test 3: Multiple Methods Requiring Different Thunk Offsets
// ============================================================================

class MultiBase1 {
public:
  virtual void method1() {}
  int data1;
};

class MultiBase2 {
public:
  virtual void method2a() {}
  virtual void method2b() {}
  int data2;
};

class MultiDerived : public MultiBase1, public MultiBase2 {
public:
  void method2a() override {}
  void method2b() override {}
};

void testMulti() {
  MultiDerived d;
  MultiBase2* b = &d;
  b->method2a();
  b->method2b();
}

// ============================================================================
// CIR Checks - Vtables with thunk references
// ============================================================================

// CIR-DAG: cir.global{{.*}}linkonce_odr comdat  @_ZTV11DeepDerived = #cir.vtable
// CIR-DAG: cir.global{{.*}}linkonce_odr comdat  @_ZTV12EmptyDerived = #cir.vtable
// CIR-DAG: cir.global{{.*}}linkonce_odr comdat  @_ZTV12MultiDerived = #cir.vtable

// CIR: cir.func {{.*}} @_ZN11DeepDerivedC2Ev
// CIR:   cir.vtable.address_point(@_ZTV11DeepDerived, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   cir.vtable.address_point(@_ZTV11DeepDerived, address_point = <index = 1, offset = 2>) : !cir.vptr

// CIR: cir.func {{.*}} @_ZThn16_N11DeepDerived8method2bEv
// CIR:   cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!u8i>, !s64i) -> !cir.ptr<!u8i>
// CIR:   cir.call @_ZN11DeepDerived8method2bEv

// ============================================================================
// LLVM Checks
// ============================================================================

// LLVM: @_ZTV11DeepDerived = linkonce_odr global { [6 x ptr], [3 x ptr] }
// LLVM-SAME: @_ZThn16_N11DeepDerived8method2bEv

// ============================================================================
// LLVM Thunk Implementation Checks
// ============================================================================

// Deep hierarchy thunk
// LLVM-LABEL: define linkonce_odr void @_ZThn{{[0-9]+}}_N11DeepDerived8method2bEv
//       LLVM: getelementptr i8, ptr %{{[0-9]+}}, i64 -{{[0-9]+}}
//       LLVM: call void @_ZN11DeepDerived8method2bEv

// Empty base thunk
// LLVM-LABEL: define linkonce_odr void @_ZThn{{[0-9]+}}_N12EmptyDerived14nonEmptyMethodEv
//       LLVM: getelementptr i8, ptr %{{[0-9]+}}, i64 -{{[0-9]+}}
//       LLVM: call void @_ZN12EmptyDerived14nonEmptyMethodEv

// Multiple methods thunks
// LLVM-LABEL: define linkonce_odr void @_ZThn{{[0-9]+}}_N12MultiDerived8method2aEv
//       LLVM: getelementptr i8, ptr %{{[0-9]+}}, i64 -{{[0-9]+}}
//       LLVM: call void @_ZN12MultiDerived8method2aEv

// LLVM-LABEL: define linkonce_odr void @_ZThn{{[0-9]+}}_N12MultiDerived8method2bEv
//       LLVM: getelementptr i8, ptr %{{[0-9]+}}, i64 -{{[0-9]+}}
//       LLVM: call void @_ZN12MultiDerived8method2bEv

// ============================================================================
// OGCG Checks - thunks exist in OG codegen
// ============================================================================

//      OGCG: @_ZTV11DeepDerived = linkonce_odr {{.*}} constant
// OGCG-SAME: @_ZThn{{[0-9]+}}_N11DeepDerived8method2bEv

//      OGCG: @_ZTV12EmptyDerived = linkonce_odr {{.*}} constant
// OGCG-SAME: @_ZThn{{[0-9]+}}_N12EmptyDerived14nonEmptyMethodEv

//     OGCG: @_ZTV12MultiDerived = linkonce_odr {{.*}} constant
// OGCG-DAG: @_ZThn{{[0-9]+}}_N12MultiDerived8method2aEv
// OGCG-DAG: @_ZThn{{[0-9]+}}_N12MultiDerived8method2bEv

// OGCG-LABEL: define linkonce_odr void @_ZThn{{[0-9]+}}_N11DeepDerived8method2bEv
//       OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 -{{[0-9]+}}
//       OGCG: call void @_ZN11DeepDerived8method2bEv
