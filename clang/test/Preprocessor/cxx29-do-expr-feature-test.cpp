// RUN: %clang_cc1 -std=c++2d -E -dM %s | FileCheck --check-prefix=CXX29 %s
// RUN: %clang_cc1 -std=c++2c -E -dM %s | FileCheck --check-prefix=NO-DO-EXPR %s
// RUN: %clang_cc1 -std=c++23 -E -dM %s | FileCheck --check-prefix=NO-DO-EXPR %s

// P2806 asks for `__cpp_do_expressions`. The paper is not adopted, so there is
// no plenary date to use as the value; 1 is what __cpp_modules uses for the
// same reason.

// CXX29: #define __cpp_do_expressions 1
// NO-DO-EXPR-NOT: __cpp_do_expressions
