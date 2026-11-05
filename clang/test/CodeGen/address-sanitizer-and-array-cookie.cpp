// RUN: %clang_cc1 -triple x86_64-gnu-linux -emit-llvm -o - %s | FileCheck %s -check-prefix=PLAIN
// RUN: %clang_cc1 -triple x86_64-gnu-linux -emit-llvm -o - -fsanitize=address %s | FileCheck %s -check-prefix=ASAN
// RUN: %clang_cc1 -triple x86_64-gnu-linux -emit-llvm -o - -fsanitize=address -fsanitize-address-poison-custom-array-cookie %s | FileCheck %s -check-prefix=ASAN-POISON-ALL-NEW-ARRAY
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-gnu-linux -fclangir -emit-llvm -o - %s | FileCheck %s -check-prefix=CIR-PLAIN %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-gnu-linux -fclangir -emit-llvm -o - -fsanitize=address %s | FileCheck %s -check-prefix=CIR-ASAN %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-gnu-linux -fclangir -emit-llvm -o - -fsanitize=address -fsanitize-address-poison-custom-array-cookie %s | FileCheck %s -check-prefix=CIR-ASAN-POISON-ALL-NEW-ARRAY %}

typedef __typeof__(sizeof(0)) size_t;
namespace std {
  struct nothrow_t {};
  std::nothrow_t nothrow;
}
void *operator new[](size_t, const std::nothrow_t &) throw();
void *operator new[](size_t, char *);
void *operator new[](size_t, int, int);

struct C {
  int x;
  ~C();
};

C *CallNew() {
  return new C[10];
}
// PLAIN-LABEL: CallNew
// PLAIN-NOT: nosanitize
// PLAIN-NOT: __asan_poison_cxx_array_cookie
// CIR-PLAIN-LABEL: CallNew
// CIR-PLAIN-NOT: nosanitize
// CIR-PLAIN-NOT: __asan_poison_cxx_array_cookie
// ASAN-LABEL: CallNew
// ASAN: store{{.*}}nosanitize
// ASAN-NOT: nosanitize
// ASAN: call void @__asan_poison_cxx_array_cookie
// CIR-ASAN-LABEL: CallNew
// CIR-ASAN: call void @__asan_poison_cxx_array_cookie

C *CallNewNoThrow() {
  return new (std::nothrow) C[10];
}
// PLAIN-LABEL: CallNewNoThrow
// PLAIN-NOT: nosanitize
// PLAIN-NOT: __asan_poison_cxx_array_cookie
// CIR-PLAIN-LABEL: CallNewNoThrow
// CIR-PLAIN-NOT: nosanitize
// CIR-PLAIN-NOT: __asan_poison_cxx_array_cookie
// ASAN-LABEL: CallNewNoThrow
// ASAN: store{{.*}}nosanitize
// ASAN-NOT: nosanitize
// ASAN: call void @__asan_poison_cxx_array_cookie
// CIR-ASAN-LABEL: CallNewNoThrow
// CIR-ASAN: call void @__asan_poison_cxx_array_cookie

void CallDelete(C *c) {
  delete [] c;
}

// PLAIN-LABEL: CallDelete
// PLAIN-NOT: nosanitize
// CIR-PLAIN-LABEL: CallDelete
// CIR-PLAIN-NOT: nosanitize
// ASAN-LABEL: CallDelete
// ASAN-NOT: nosanitize
// ASAN: call i64 @__asan_load_cxx_array_cookie
// ASAN-NOT: nosanitize
// CIR-ASAN-LABEL: CallDelete
// CIR-ASAN: call i64 @__asan_load_cxx_array_cookie

char Buffer[20];
C *CallPlacementNew() {
  return new (Buffer) C[20];
}
// ASAN-LABEL: CallPlacementNew
// ASAN-NOT: __asan_poison_cxx_array_cookie
// CIR-ASAN-LABEL: CallPlacementNew
// CIR-ASAN-NOT: __asan_poison_cxx_array_cookie

C *CallNewWithArgs() {
// ASAN-LABEL: CallNewWithArgs
// ASAN-NOT: call void @__asan_poison_cxx_array_cookie
// ASAN-POISON-ALL-NEW-ARRAY: call void @__asan_poison_cxx_array_cookie
// CIR-ASAN-LABEL: CallNewWithArgs
// CIR-ASAN-NOT: call void @__asan_poison_cxx_array_cookie
// CIR-ASAN-POISON-ALL-NEW-ARRAY: call void @__asan_poison_cxx_array_cookie
  return new (123, 456) C[20];
}
