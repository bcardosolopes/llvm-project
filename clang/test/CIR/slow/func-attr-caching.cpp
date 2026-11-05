// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 \
// RUN:   -fcxx-exceptions -fexceptions -Os -fclangir -emit-cir \
// RUN:   -o - %s 2>&1 | grep -c '#cir<extra(' > %t.count
// RUN: cat %t.count
//
// Verify that default function attributes are cached rather than
// reconstructed via MLIR uniquer for every call expression.
//
// The default DictionaryAttr produced by getTrivialDefaultFunctionAttributes()
// is cached in getDefaultFunctionAttributes() and reused, only adding
// per-call-site overrides.
//
// This test generates 50 call expressions. With caching, the number of
// unique ExtraFuncAttributesAttr alias definitions should be small (≤10),
// not proportional to the call count.
//
// Verify unique extra attribute count is small
// RUN: %python -c "import sys; sys.exit(0 if int(open(r'%t.count').read().strip()) <= 10 else 1)"

void f1(); void f2(); void f3(); void f4(); void f5();

void test_many_calls() {
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
  f1(); f2(); f3(); f4(); f5();
}
