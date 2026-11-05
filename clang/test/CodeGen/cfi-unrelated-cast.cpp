// STL allocators should not have unrelated-cast tests applied
// RUN: %clang_cc1 -flto -triple x86_64-unknown-linux -fvisibility=hidden -fsanitize=cfi-unrelated-cast -emit-llvm -o - %s | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -flto -triple x86_64-unknown-linux -fvisibility=hidden -fsanitize=cfi-unrelated-cast -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR %}

#include <stddef.h>

template<class T>
class myalloc {
 public:
  // CHECK: define{{.*}}allocateE{{.}}
  // CHECK-NOT: llvm.type.test
  T *allocate(size_t sz) {
    return (T*)::operator new(sz);
  }

  // CHECK: define{{.*}}allocateE{{.}}PKv
  // CHECK-NOT: llvm.type.test
  T *allocate(size_t sz, const void *ptr) {
    return (T*)::operator new(sz);
  }

  // CHECK: define{{.*}}differentName
  // CHECK: llvm.type.test
  T *differentName(size_t sz, const void *ptr) {
    return (T*)::operator new(sz);
  }
};

class C1 {
  virtual void f() {}
};

void f1() {
  myalloc<C1> allocator;
  (void)allocator.allocate(16);
  (void)allocator.allocate(16, 0);
  (void)allocator.differentName(16, 0);
}

// CIR-LABEL: define linkonce_odr hidden noundef ptr @_ZN7myallocI2C1E13differentNameEmPKv(
// CIR: call i1 @llvm.type.test(ptr {{.*}}, metadata !"_ZTS2C1")
// CIR: call i1 @llvm.type.test(ptr {{.*}}, metadata !"all-vtables")
// CIR: call void @__ubsan_handle_cfi_check_fail_abort(ptr {{.*}}, i64 {{.*}}, i64 {{.*}}) #[[ATTR:[0-9]+]]

// CIR-LABEL: define linkonce_odr hidden noundef ptr @_ZN7myallocI2C1E8allocateEmPKv(
// CIR-NOT: llvm.type.test
// CIR: ret ptr

// CIR-LABEL: define linkonce_odr hidden noundef ptr @_ZN7myallocI2C1E8allocateEm(
// CIR-NOT: llvm.type.test
// CIR: ret ptr
// CIR: attributes #[[ATTR]] = { nomerge noreturn nounwind }
