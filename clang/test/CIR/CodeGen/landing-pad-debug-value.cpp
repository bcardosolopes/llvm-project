// RUN: %clang_cc1 -std=c++20 -Oz -triple aarch64-none-linux-android24 \
// RUN:   -fclangir -fclangir-mem2reg -emit-obj -disable-free \
// RUN:   -gkey-instructions -debug-info-kind=constructor -dwarf-version=4 \
// RUN:   -fcxx-exceptions -fexceptions \
// RUN:   -mrelocation-model pic -pic-level 2 \
// RUN:   %s -o /dev/null
//
// RUN: %clang_cc1 -std=c++20 -Oz -triple aarch64-none-linux-android24 \
// RUN:   -fclangir -fclangir-mem2reg -emit-llvm -disable-llvm-passes \
// RUN:   -gkey-instructions -debug-info-kind=constructor -dwarf-version=4 \
// RUN:   -fcxx-exceptions -fexceptions \
// RUN:   -mrelocation-model pic -pic-level 2 \
// RUN:   %s -o %t.ll
// RUN: FileCheck %s < %t.ll

// Verify that CIR does not place #dbg_value records between PHI nodes and
// the landingpad instruction in EH landing pad blocks. Such placement is
// invalid in LLVM IR and causes SplitLandingPadPredecessors to assert
// "Inserting PHI after debug-records!".

namespace std {
class exception {
public:
  virtual const char *what() const noexcept;
};
class bad_alloc : public exception {};
class runtime_error : public exception {
public:
  runtime_error(const char *);
};
} // namespace std

extern "C" void *malloc(unsigned long);
extern "C" void free(void *);

// Multiple throwing paths that converge on a single landing pad with PHI nodes
// for variables promoted by mem2reg.
static bool parse(const char **out, int *outLen) {
  char **syms = nullptr;
  int count = 0;
  *out = nullptr;
  *outLen = 0;

  try {
    syms = (char **)malloc(64);
    if (!syms)
      throw std::bad_alloc();

    // Two throw paths with different 'count' values force a PHI in the
    // landing pad block.
    count = 1;
    if (!syms[0])
      throw std::runtime_error("first");
    count = 2;
    if (!syms[1])
      throw std::runtime_error("second");

    *out = (const char *)syms;
    *outLen = count;
    return true;
  } catch (std::exception &e) {
    for (int i = 0; i < count; i++)
      free(syms[i]);
    free(syms);
    return false;
  }
}

bool test() {
  const char *s;
  int n;
  return parse(&s, &n);
}

// CHECK-LABEL: define internal
// Verify no #dbg_value appears between a phi and landingpad in any block.
// The pattern we must NOT see is: phi ... \n #dbg_value ... \n landingpad
// After the fix, debug records come after the landingpad instruction.
//
// CHECK: landingpad
// CHECK-NOT: phi {{.*}} #dbg_value {{.*}} landingpad
