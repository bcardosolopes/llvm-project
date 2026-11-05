// Verify that ClangIR honors -mlong-double-64 / -mlong-double-128 for the
// `long double` RETURN type on x86_64, matching classic CodeGen (OGCG). The
// return value must be classified with the overridden long double format
// (double for -mlong-double-64, fp128 for -mlong-double-128), not the target
// native x86_fp80. Previously the CIR target-lowering pass rebuilt a
// TargetInfo without applying the override, so it kept x87DoubleExtended() and
// coerced the return to x86_fp80 (a silent ABI miscompile), while parameters
// were classified correctly. The default (no flag) native x86_fp80 return must
// stay unchanged.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mlong-double-64 -fclangir -emit-llvm %s -o %t.ld64.ll
// RUN: FileCheck %s --check-prefix=LD64 < %t.ld64.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mlong-double-64 -emit-llvm %s -o %t.ld64.ogcg.ll
// RUN: FileCheck %s --check-prefix=LD64 < %t.ld64.ogcg.ll

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mlong-double-128 -fclangir -emit-llvm %s -o %t.ld128.ll
// RUN: FileCheck %s --check-prefix=LD128 < %t.ld128.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mlong-double-128 -emit-llvm %s -o %t.ld128.ogcg.ll
// RUN: FileCheck %s --check-prefix=LD128 < %t.ld128.ogcg.ll

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.def.ll
// RUN: FileCheck %s --check-prefix=DEF < %t.def.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.def.ogcg.ll
// RUN: FileCheck %s --check-prefix=DEF < %t.def.ogcg.ll

// The return type honors the override; params were already correct.

// LD64: define dso_local double @ret()
long double ret(void) { return 1.5L; }

// LD64: define dso_local double @id(double {{.*}}%{{.*}})
long double id(long double x) { return x; }

// Return + param + arithmetic: both slots use the overridden format.
// LD64: define dso_local double @add(double {{.*}}%{{.*}}, double {{.*}}%{{.*}})
long double add(long double x, long double y) { return x + y; }

// -mlong-double-128: return and param are fp128.

// LD128: define dso_local fp128 @ret()
// LD128: define dso_local fp128 @id(fp128 {{.*}}%{{.*}})
// LD128: define dso_local fp128 @add(fp128 {{.*}}%{{.*}}, fp128 {{.*}}%{{.*}})

// Default (no override): native x86_fp80 return is unchanged.

// DEF: define dso_local x86_fp80 @ret()
// DEF: define dso_local x86_fp80 @id(x86_fp80 {{.*}}%{{.*}})
// DEF: define dso_local x86_fp80 @add(x86_fp80 {{.*}}%{{.*}}, x86_fp80 {{.*}}%{{.*}})
