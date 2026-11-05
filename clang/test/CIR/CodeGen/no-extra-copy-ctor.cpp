// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// CIR emits extra unused copy constructor definitions that OGCG does
// not. When a type has an implicitly-declared copy constructor that is never
// called in the translation unit, OGCG does not emit it. CIR emits it with
// linkonce_odr linkage.
//
// Fixed by implementing isMemcpyEquivalentSpecialMember in CIR, which converts
// trivial copy/move constructor calls into aggregate copies (memcpy) instead of
// function calls, avoiding the creation of function references that trigger
// deferred emission of the constructor definition.

struct QChar {
  unsigned short X;
  QChar(unsigned short);
};

struct Command {
  Command(QChar c) : c(c) {}
  unsigned int type : 4;
  QChar c;
};

Command X(QChar('c'));

void Foo(QChar);
void bar() { Foo(X.c); }

// OGCG does not emit _ZN5QCharC2ERKS_ (QChar copy ctor) at all.
// CIR emits: define linkonce_odr dso_local void @_ZN5QCharC2ERKS_(...) comdat
// CHECK-NOT: @_ZN5QCharC2ERKS_
