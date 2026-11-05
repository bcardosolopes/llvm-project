// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o - 2>&1 | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - 2>&1 | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - 2>&1 | FileCheck %s --check-prefix=OGCG
class Base {
  // CIR-LABEL: _ZN4BaseaSERS_
  // CIR-SAME: ([[ARG0:%.*]]: !cir.ptr{{.*}}, [[ARG1:%.*]]: !cir.ptr{{.*}})
  // CIR: [[ALLOCA_0:%.*]] =  cir.alloca
  // CIR: [[ALLOCA_1:%.*]] =  cir.alloca
  // CIR: cir.store [[ARG0]], [[ALLOCA_0]]
  // CIR: cir.store [[ARG1]], [[ALLOCA_1]]
  // CIR: [[LD_0:%.*]] = cir.load deref [[ALLOCA_0]]
  // CIR: cir.return [[LD_0]]

  // LLVM-LABEL: @_ZN4BaseaSERS_
  // LLVM-SAME: (ptr noundef nonnull align 1 dereferenceable(1) [[ARG0:%.*]], ptr noundef nonnull align 1 dereferenceable(1) [[ARG1:%.*]])
  // LLVM:       [[TMP3:%.*]] = alloca ptr, i64 1, align 8
  // LLVM-NEXT:  store ptr [[ARG1]], ptr [[TMP3]], align 8
  // LLVM-NEXT:  ret ptr [[ARG0]]

public:
  Base &operator=(Base &b) {
    return *this;
  }
};

class Derived : Base {
  Derived &operator=(Derived &);
};
Derived &Derived::operator=(Derived &B) {
  // CIR-LABEL: _ZN7DerivedaSERS_
  // CIR-SAME: [[ARG0:%.*]]: !cir.ptr{{.*}}, [[ARG1:%.*]]: !cir.ptr{{.*}}
  // CIR: cir.store [[ARG0]], [[ALLOCA_0:%.*]] :
  // CIR: cir.store [[ARG1]], [[ALLOCA_1:%.*]] :
  // CIR: [[LD_0:%.*]] = cir.load [[ALLOCA_0]]
  // CIR: [[BASE_ADDR_0:%.*]] = cir.base_class_addr [[LD_0]]
  // CIR: [[LD_1:%.*]] = cir.load [[ALLOCA_1]]
  // CIR: [[BASE_ADDR_1:%.*]] = cir.base_class_addr [[LD_1]]
  // CIR: [[CALL:%.*]] = cir.call @_ZN4BaseaSERS_
  // CIR: [[DERIVED_ADDR:%.*]] = cir.derived_class_addr [[CALL]]
  // CIR: cir.return [[DERIVED_ADDR]]

  // LLVM-LABEL: _ZN7DerivedaSERS_
  // LLVM-SAME: (ptr noundef nonnull align 1 dereferenceable(1) [[ARG0:%.*]], ptr noundef nonnull align 1 dereferenceable(1) [[ARG1:%.*]])
  // LLVM:       [[TMP3:%.*]] = alloca ptr, i64 1, align 8
  // LLVM-NEXT:  [[TMP4:%.*]] = alloca ptr, i64 1, align 8
  // LLVM-NEXT:  store ptr [[ARG0]], ptr [[TMP3]], align 8
  // LLVM-NEXT:  store ptr [[ARG1]], ptr [[TMP4]], align 8
  // LLVM-NEXT:  [[TMP5:%.*]] = load ptr, ptr [[TMP3]], align 8
  // LLVM-NEXT:  [[TMP6:%.*]] = load ptr, ptr [[TMP4]], align 8
  // LLVM-NEXT:  [[TMP7:%.*]] = call noundef nonnull align 1 dereferenceable(1) ptr @_ZN4BaseaSERS_(ptr noundef nonnull align 1 dereferenceable(1) [[TMP5]], ptr noundef nonnull align 1 dereferenceable(1) [[TMP6]])
  // LLVM-NEXT:  ret ptr [[TMP7]]

  // OGCG-LABEL: @_ZN7DerivedaSERS_
  // OGCG-SAME: (ptr{{.*}}[[ARG0:%.*]], ptr{{.*}}[[ARG1:%.*]])
  // OGCG:       [[TMP3:%.*]] = alloca ptr, align 8
  // OGCG-NEXT:  [[TMP4:%.*]] = alloca ptr, align 8
  // OGCG-NEXT:  store ptr [[ARG0]], ptr [[TMP3]], align 8
  // OGCG-NEXT:  store ptr [[ARG1]], ptr [[TMP4]], align 8
  // OGCG-NEXT:  [[TMP5:%.*]] = load ptr, ptr [[TMP3]], align 8
  // OGCG-NEXT:  [[TMP6:%.*]] = load ptr, ptr [[TMP4]], align 8
  // OGCG-NEXT:  [[TMP7:%.*]] = call{{.*}}ptr @_ZN4BaseaSERS_(ptr{{.*}}[[TMP5]], ptr{{.*}}[[TMP6]])
  // OGCG-NEXT:  ret ptr [[TMP7]]
  return (Derived &)Base::operator=(B);
}

// OGCG-LABEL: define{{.*}}@_ZN4BaseaSERS_
// OGCG-SAME: (ptr{{.*}}[[BASE_ARG0:%.*]], ptr{{.*}}[[BASE_ARG1:%.*]])
// OGCG:       [[TMP3:%.*]] = alloca ptr, align 8
// OGCG-NEXT:  [[TMP4:%.*]] = alloca ptr, align 8
// OGCG-NEXT:  store ptr [[BASE_ARG0]], ptr [[TMP3]], align 8
// OGCG-NEXT:  store ptr [[BASE_ARG1]], ptr [[TMP4]], align 8
// OGCG-NEXT:  [[TMP5:%.*]] = load ptr, ptr [[TMP3]], align 8
// OGCG-NEXT:  ret ptr [[TMP5]]
