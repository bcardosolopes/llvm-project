// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-obj \
// RUN:   -std=gnu++20 -debug-info-kind=constructor -dwarf-version=4 \
// RUN:   -o /dev/null %s 2>&1 | FileCheck --allow-empty %s
// CHECK-NOT: Assertion

// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm -std=gnu++20 -debug-info-kind=constructor -dwarf-version=4 \
// RUN:   -o - %s | FileCheck --check-prefix=CONST-VOID %s

// Verify that CIR debug info lowering handles:
// 1. Dependent types (template parameters) in method signatures
// 2. const void* parameters (void pointee -> null -> DW_TAG_const_type wrapping null)
// 3. Forward-declared CXXRecordDecl without definition (rebuildSourceDebugRecordFlags)

template <typename T>
struct Wrapper {
  T value;
  void set(const T &v) { value = v; }
  T get() const { return value; }
};

struct Container {
  Wrapper<int> w;

  void annotate(const void *a, const void *b, const void *c) {}

  void use() {
    w.set(42);
    annotate(nullptr, nullptr, nullptr);
  }
};

void test() {
  Container c;
  c.use();
}

// CONST-VOID-DAG: !DIDerivedType(tag: DW_TAG_const_type, baseType: null)
