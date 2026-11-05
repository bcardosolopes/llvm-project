// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o /dev/null

// __builtin_extract_return_addr — identity on non-Sparc (fixed)
void *test(void) {
  return __builtin_extract_return_addr(__builtin_return_address(0));
}
