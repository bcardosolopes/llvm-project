// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -O1 \
// RUN:            -debug-info-kind=constructor -gkey-instructions \
// RUN:            -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -O1 \
// RUN:            -debug-info-kind=constructor -gkey-instructions \
// RUN:            -emit-llvm %s -o - | FileCheck %s

// Test that compiler-generated destructors (D0/D1/D2) get
// keyInstructions=true on their DISubprograms when -gkey-instructions
// is enabled, matching OGCG behavior.

namespace std {
  class exception {
  public:
    virtual ~exception() noexcept;
    virtual const char* what() const noexcept;
  };
}

std::exception::~exception() noexcept {}
const char* std::exception::what() const noexcept { return "std::exception"; }

// CHECK-DAG: distinct !DISubprogram({{.*}}_ZNSt9exceptionD2Ev{{.*}}keyInstructions: true)
// CHECK-DAG: distinct !DISubprogram({{.*}}_ZNSt9exceptionD0Ev{{.*}}keyInstructions: true)
