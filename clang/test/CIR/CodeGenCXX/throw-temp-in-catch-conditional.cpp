// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -fcxx-exceptions -fexceptions -emit-obj %s -o %t.o

//
// This exercises throw emission inside a catch with both:
//  - an outer EH cleanup already on the EH scope stack, and
//  - a throw expression that introduces temporary cleanups.
// CIRGen previously crashed while walking EH scopes for the synthesized
// __cxa_throw call cleanup region.

struct Temp {
  Temp();
  Temp(const Temp &);
  ~Temp();
};

Temp makeTemp();

struct Outer {
  ~Outer();
};

void f(bool b) {
  try {
    throw 1;
  } catch (...) {
    Outer o;
    if (b)
      throw Temp(makeTemp());
  }
}

