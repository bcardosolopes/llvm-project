// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O2 %s -o - | FileCheck %s

// When a struct parameter (e.g., StringRef) is coerced into multiple register
// args ({ptr, i64}), pointer-only attributes (nonnull, align, dereferenceable)
// for subsequent reference parameters must be placed on the correct coerced arg,
// not on the i64 portion of the expanded struct.

struct StringRef {
  const char *Data;
  unsigned long Length;
};

struct APSInt {
  unsigned long long Val;
  unsigned Width;
  bool IsUnsigned;
};

struct ScopedPrinter {
  int pad[11];
  virtual void printNumber(StringRef Label, const APSInt &Value);
};

void test(ScopedPrinter &W, const APSInt &V) {
  W.printNumber(StringRef{"Value", 5}, V);
}

// CHECK-LABEL: define {{.*}} @_Z4testR13ScopedPrinterRK6APSInt
// The virtual call should have:
//   arg0 (ptr): nonnull align 8 dereferenceable(52) — ScopedPrinter this
//   arg1 (ptr): nonnull — StringRef.Data
//   arg2 (i64): NO pointer attrs — StringRef.Length
//   arg3 (ptr): nonnull align 8 dereferenceable(16) — APSInt ref
// CHECK: call void %{{[0-9]+}}(ptr noundef nonnull align 8 dereferenceable(52) %{{.*}}, ptr nonnull @{{.*}}, i64 5, ptr noundef nonnull align 8 dereferenceable(16) %{{.*}})
