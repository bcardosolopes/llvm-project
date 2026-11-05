// Test that aligned new-expressions with __hot_cold_t get align N on the call
// return value, matching OGCG. This was previously missing because
// cir.assume_aligned was removed by DCE (it was marked Pure).
//
// RUN: %clang_cc1 -std=c++14 -triple x86_64-unknown-linux-gnu -faligned-allocation \
// RUN:   -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++14 -triple x86_64-unknown-linux-gnu -faligned-allocation \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

typedef __typeof__(sizeof(0)) size_t;
namespace std {
enum class align_val_t : size_t;
} // namespace std

typedef unsigned char uint8_t;
enum class __hot_cold_t : uint8_t;

void *operator new(size_t size, std::align_val_t alignment,
                   __hot_cold_t hot_cold) noexcept(false);

class alignas(32) alignedstruct {
  int x;
};

void test_aligned_new_hot_cold() {
  // Explicit operator new call — always had align 32.
  // CIR: call noalias noundef nonnull align 32 ptr @_ZnwmSt11align_val_t12__hot_cold_t(i64 noundef 1, i64 noundef 32, i8 noundef zeroext 0)
  // OGCG: call noalias noundef nonnull align 32 ptr @_ZnwmSt11align_val_t12__hot_cold_t(i64 noundef 1, i64 noundef 32, i8 noundef zeroext 0)
  operator new(1, (std::align_val_t)32, (__hot_cold_t)0);

  // New-expression with builtin attribute — was missing align 32 before fix.
  // CIR: call noalias noundef nonnull align 32 ptr @_ZnwmSt11align_val_t12__hot_cold_t(i64 noundef 32, i64 noundef 32, i8 noundef zeroext 0)
  // OGCG: call noalias noundef nonnull align 32 ptr @_ZnwmSt11align_val_t12__hot_cold_t(i64 noundef 32, i64 noundef 32, i8 noundef zeroext 0)
  new ((__hot_cold_t)0) alignedstruct;
}
