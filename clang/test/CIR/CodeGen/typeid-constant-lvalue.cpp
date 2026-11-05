// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test constant lvalue emission of typeid expressions.

namespace std {
  class type_info {
  public:
    virtual ~type_info();
    const char *name() const;
  };
}

struct Base {
    virtual ~Base() = default;
};

// typeid on a non-polymorphic type (constant-foldable)
const std::type_info &intInfo = typeid(int);
// CHECK: cir.global  constant external  @intInfo = #cir.global_view<@_ZTIi> : !cir.ptr<!rec_std3A3Atype_info>

// typeid on a polymorphic type (still constant when applied to a type, not expr)
const std::type_info &baseInfo = typeid(Base);
// CHECK: cir.global  constant external  @baseInfo = #cir.global_view<@_ZTI4Base> : !cir.ptr<!rec_std3A3Atype_info>
