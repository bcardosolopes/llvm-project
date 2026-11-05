// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fsanitize=cfi-icall -Wno-cfi-unchecked-callee -fclangir -emit-cir -o - %s | FileCheck --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fsanitize=cfi-icall -Wno-cfi-unchecked-callee -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=LLVM %s

#define CFI_UNCHECKED_CALLEE __attribute__((cfi_unchecked_callee))

void unchecked(void) CFI_UNCHECKED_CALLEE {}

// CIR: cir.global  external  @checked = #cir.global_view<@_Z9uncheckedv, no_cfi = true>
// LLVM: @checked = global ptr no_cfi @_Z9uncheckedv, align 8
void (*checked)(void) = unchecked;

// CIR: cir.global  external  @unchecked2 = #cir.global_view<@_Z9uncheckedv, no_cfi = true>
// LLVM: @unchecked2 = global ptr no_cfi @_Z9uncheckedv, align 8
void (CFI_UNCHECKED_CALLEE *unchecked2)(void) = unchecked;

// CIR: cir.global  external  @checked2 = #cir.global_view<@_Z9uncheckedv, no_cfi = true>
// LLVM: @checked2 = global ptr no_cfi @_Z9uncheckedv, align 8
constexpr void (CFI_UNCHECKED_CALLEE *unchecked_constexpr)(void) = unchecked;
void (*checked2)(void) = unchecked_constexpr;

// CIR: cir.global  external  @checked_array = #cir.const_array<[#cir.global_view<@_Z9uncheckedv, no_cfi = true>
// LLVM: @checked_array = global [2 x ptr] [ptr no_cfi @_Z9uncheckedv, ptr no_cfi @_Z9uncheckedv], align 16
void (*checked_array[])(void) = {
    unchecked,
    reinterpret_cast<void (*)(void)>(unchecked),
};

void func_accepting_checked(void (*p)(void)) {}

// LLVM-LABEL: define dso_local void @_Z9InvokeCFIv()
// LLVM: [[CHECKED:%.*]] = load ptr, ptr @checked, align 8
// LLVM-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[CHECKED]], metadata !"_ZTSFvvE")
void InvokeCFI() { checked(); }

// LLVM-LABEL: define dso_local void @_Z11InvokeNoCFIv()
// LLVM: [[UNCHECKED:%.*]] = load ptr, ptr @unchecked2, align 8
// LLVM-NEXT: call void [[UNCHECKED]]()
void InvokeNoCFI() { unchecked2(); }

struct A {
  void unchecked_method() CFI_UNCHECKED_CALLEE {}
  static void unchecked_static_method() CFI_UNCHECKED_CALLEE {}
  int unchecked_const_method() const CFI_UNCHECKED_CALLEE { return 0; }
  int unchecked_const_method_int_arg(int n) const CFI_UNCHECKED_CALLEE {
    return n;
  }
};

void h(void) {
  // CIR: %{{.*}} = cir.get_global @_Z9uncheckedv : !cir.ptr<!cir.func<()>> {cir.no_cfi =
  // LLVM: store ptr no_cfi @_Z9uncheckedv, ptr %{{.*}}, align 8
  void (*unchecked_local)(void) = unchecked;

  // CIR: %{{.*}} = cir.get_global @_Z9uncheckedv : !cir.ptr<!cir.func<()>> {cir.no_cfi =
  // LLVM: call void @_Z22func_accepting_checkedPFvvE(ptr noundef no_cfi @_Z9uncheckedv)
  func_accepting_checked(unchecked);

  // CIR: #cir.global_view<@_ZN1A16unchecked_methodEv, no_cfi = true> : !s64i
  // LLVM: store { i64, i64 } { i64 ptrtoint (ptr no_cfi @_ZN1A16unchecked_methodEv to i64), i64 0 }, ptr %{{.*}}, align 8
  auto A1 = &A::unchecked_method;

  // CIR: %{{.*}} = cir.get_global @_ZN1A23unchecked_static_methodEv : !cir.ptr<!cir.func<()>> {cir.no_cfi =
  // LLVM: store ptr no_cfi @_ZN1A23unchecked_static_methodEv, ptr %{{.*}}, align 8
  auto A2 = &A::unchecked_static_method;

  // CIR: #cir.global_view<@_ZNK1A22unchecked_const_methodEv, no_cfi = true> : !s64i
  // LLVM: store { i64, i64 } { i64 ptrtoint (ptr no_cfi @_ZNK1A22unchecked_const_methodEv to i64), i64 0 }, ptr %{{.*}}, align 8
  auto A3 = (int(CFI_UNCHECKED_CALLEE A::*)() const)(&A::unchecked_const_method);

  // CIR: #cir.global_view<@_ZNK1A30unchecked_const_method_int_argEi, no_cfi = true> : !s64i
  // LLVM: store { i64, i64 } { i64 ptrtoint (ptr no_cfi @_ZNK1A30unchecked_const_method_int_argEi to i64), i64 0 }, ptr %{{.*}}, align 8
  auto A4 = (int(CFI_UNCHECKED_CALLEE A::*)(int) const)(&A::unchecked_const_method_int_arg);
}
