// RUN: %clang_cc1 -triple x86_64-unknown-linux -flto -flto-unit -fvisibility=hidden -fsanitize=cfi-vcall -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -flto -flto-unit -fvisibility=hidden -fsanitize=cfi-vcall -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -flto -flto-unit -fvisibility=hidden -fsanitize=cfi-vcall -fsanitize-cfi-cross-dso -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=XDSO %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -flto -flto-unit -fvisibility=hidden -fsanitize=cfi-vcall -fsanitize-cfi-cross-dso -emit-llvm -o - %s | FileCheck --check-prefix=XDSO-OGCG %s

// A non-trapping vtable-based CFI checker (cfi-vcall here) enables the
// "all-vtables" bitset. OGCG's CodeGenModule::AddVTableTypeMetadata attaches an
// "all-vtables" !type entry at each address-point offset of every vtable global;
// ClangIR must do the same so the producer bitset matches the
// llvm.type.test(vtable, "all-vtables") consumer emitted in the cfi_check_fail
// diagnostic path. Under -fsanitize-cfi-cross-dso a numeric type id (the MD5
// hash of the mangled type name) is also attached at each address point.

struct Base {
  virtual void f();
};

struct Derived : Base {
  void f() override;
};

void Base::f() {}
void Derived::f() {}

Derived d;

// The Derived vtable carries the base id + "all-vtables" + derived id, with
// "all-vtables" repeated at each of the two address points (Base and Derived).
// LLVM: @_ZTV7Derived ={{.*}}constant {{.*}} !type ![[BASE_ID:[0-9]+]], !type ![[ALLVT:[0-9]+]], !type ![[DERIVED_ID:[0-9]+]], !type ![[ALLVT]], !type {{![0-9]+}}, !type {{![0-9]+}}
// The Base vtable carries the base type id and "all-vtables" at offset 16.
// LLVM: @_ZTV4Base ={{.*}}constant {{.*}}, !type ![[BASE_ID]], !type ![[ALLVT]], !type {{![0-9]+}}

// OGCG: @_ZTV7Derived ={{.*}}constant {{.*}} !type ![[OGCG_BASE_ID:[0-9]+]], !type ![[OGCG_ALLVT:[0-9]+]], !type {{![0-9]+}}, !type ![[OGCG_DERIVED_ID:[0-9]+]], !type ![[OGCG_ALLVT]], !type {{![0-9]+}}
// OGCG: @_ZTV4Base ={{.*}}constant {{.*}}, !type ![[OGCG_BASE_ID]], !type ![[OGCG_ALLVT]], !type {{![0-9]+}}

// LLVM-DAG: ![[BASE_ID]] = !{i64 16, !"_ZTS4Base"}
// LLVM-DAG: ![[ALLVT]] = !{i64 16, !"all-vtables"}
// LLVM-DAG: ![[DERIVED_ID]] = !{i64 16, !"_ZTS7Derived"}

// OGCG-DAG: ![[OGCG_BASE_ID]] = !{i64 16, !"_ZTS4Base"}
// OGCG-DAG: ![[OGCG_ALLVT]] = !{i64 16, !"all-vtables"}
// OGCG-DAG: ![[OGCG_DERIVED_ID]] = !{i64 16, !"_ZTS7Derived"}

// Cross-DSO: the numeric id (MD5 hash of the mangled name) is attached next to
// the address-point type id, and must match OGCG exactly.
// XDSO: @_ZTV4Base ={{.*}}constant {{.*}}, !type ![[X_BASE_ID:[0-9]+]], !type ![[X_BASE_NUM:[0-9]+]], !type ![[X_ALLVT:[0-9]+]], !type {{![0-9]+}}
// XDSO-DAG: ![[X_BASE_ID]] = !{i64 16, !"_ZTS4Base"}
// XDSO-DAG: ![[X_BASE_NUM]] = !{i64 16, i64 2370067920813235120}
// XDSO-DAG: ![[X_ALLVT]] = !{i64 16, !"all-vtables"}

// XDSO-OGCG: @_ZTV4Base ={{.*}}constant {{.*}}, !type ![[XO_BASE_ID:[0-9]+]], !type ![[XO_BASE_NUM:[0-9]+]], !type ![[XO_ALLVT:[0-9]+]], !type {{![0-9]+}}
// XDSO-OGCG-DAG: ![[XO_BASE_ID]] = !{i64 16, !"_ZTS4Base"}
// XDSO-OGCG-DAG: ![[XO_BASE_NUM]] = !{i64 16, i64 2370067920813235120}
// XDSO-OGCG-DAG: ![[XO_ALLVT]] = !{i64 16, !"all-vtables"}
