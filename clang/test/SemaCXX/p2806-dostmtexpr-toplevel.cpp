// RUN: %clang_cc1 -verify -std=c++20 %s -fsyntax-only

int x = do { do_yield 42; }; // expected-error {{do expression not allowed at file scope}}