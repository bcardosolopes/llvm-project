// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Regression coverage for top-level declarations that should be ignored by
// CIRGenModule::emitTopLevelDecl instead of falling into the NYI/default path.

typedef int typedef_int;
using alias_int = typedef_int;

enum TopLevelEnum { Zero = 0, One = 1 };

struct TopLevelRecord {
  alias_int lhs;
  typedef_int rhs;
};

namespace top_level_ns {
using ::TopLevelRecord;
}

namespace aliased_ns = top_level_ns;
using namespace aliased_ns;
using enum TopLevelEnum;

int top_level_types() {
  TopLevelRecord record{One, Zero};
  return record.lhs + record.rhs;
}

// LLVM-LABEL: define{{.*}} @_Z15top_level_typesv()
// LLVM: ret i32

// OGCG-LABEL: define{{.*}} @_Z15top_level_typesv()
// OGCG: ret i32
