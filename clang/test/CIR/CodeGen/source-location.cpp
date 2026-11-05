// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

namespace std {
struct source_location {
  struct __impl {
    const char *_M_file_name;
    const char *_M_function_name;
    unsigned _M_line;
    unsigned _M_column;
  };
  const __impl *__ptr_;
  static constexpr source_location current(
      const __impl *__p = __builtin_source_location()) noexcept {
    source_location __loc;
    __loc.__ptr_ = __p;
    return __loc;
  }
};
}

// CIR: cir.global "private"  constant cir_private  @".constant" =
// CIR-SAME: #cir.const_record<{#cir.global_view<@".str">
// CIR-SAME: #cir.global_view<@".str.1">

// LLVM: @.constant = private unnamed_addr constant %"struct.std::source_location::__impl"
// LLVM-SAME: ptr @.str
// LLVM-SAME: ptr @.str.1

// OGCG: @.constant = private unnamed_addr constant %"struct.std::source_location::__impl"
// OGCG-SAME: ptr @.str
// OGCG-SAME: ptr @.str.1

int main() {
  auto loc = std::source_location::current();
  return 0;
}
