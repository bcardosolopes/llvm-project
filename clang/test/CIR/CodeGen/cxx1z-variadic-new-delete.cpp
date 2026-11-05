// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm \
// RUN:   -fexceptions -fcxx-exceptions -std=c++17 -O0 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify that variadic operator new/delete with overaligned types does not
// crash when the placement delete cleanup fires. The delete function has
// fewer declared parameters than the implicit alignment + placement args
// passed as variadic arguments.

typedef decltype(sizeof(0)) size_t;
namespace std { enum class align_val_t : size_t {}; }
#define OVERALIGNED alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__ * 2)

struct Q { int n; } q;

struct OVERALIGNED F {
  F();
  void *operator new(size_t, ...);
  void operator delete(void *, ...);
  int n[128];
};

// CHECK-LABEL: define {{.*}} @_Z2f1v(
// CHECK:   call noundef ptr (i64, ...) @_ZN1FnwEmz(i64 noundef 512, i64 noundef 32, i32
// CHECK:   invoke void @_ZN1FC1Ev(
// The cleanup should call operator delete with alignment + placement args:
// CHECK:   call void (ptr, ...) @_ZN1FdlEPvz(ptr noundef %{{.*}}, i64 noundef 32, i32
void *f1() { return new (q) F; }
