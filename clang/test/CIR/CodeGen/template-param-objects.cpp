// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

struct S { char buf[32]; };
template<S s> constexpr const char *begin() { return s.buf; }
template<S s> constexpr const char *end() { return s.buf + __builtin_strlen(s.buf); }

namespace { struct T { char buf[32]; }; }
template<T t> constexpr const char* begin_anon() { return t.buf; }

// CIR: cir.global  constant linkonce_odr comdat @_ZTAXtl1StlA32_cLc104ELc101ELc108ELc108ELc111ELc32ELc119ELc111ELc114ELc108ELc100EEEE

// LLVM: @_ZTAXtl1StlA32_cLc104ELc101ELc108ELc108ELc111ELc32ELc119ELc111ELc114ELc108ELc100EEEE = linkonce_odr constant
// LLVM-SAME: comdat

// OGCG: @_ZTAXtl1StlA32_cLc104ELc101ELc108ELc108ELc111ELc32ELc119ELc111ELc114ELc108ELc100EEEE = linkonce_odr constant
// OGCG-SAME: comdat
const char *p = begin<S{"hello world"}>();
const char *q = end<S{"hello world"}>();

// Anonymous namespace — internal linkage, no comdat.

// CIR: cir.global "private" constant internal
// CIR-NOT: comdat

// LLVM: internal constant
// LLVM-NOT: comdat

// OGCG: internal constant
// OGCG-NOT: comdat
const char *r = begin_anon<T{"hello anon"}>();
