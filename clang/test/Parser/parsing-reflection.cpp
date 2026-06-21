// RUN: %clang_cc1 %s -std=c++26 -freflection -fsyntax-only -verify
// expected-no-diagnostics

struct A{};
namespace B{};
void f(){};

consteval void test()
{
    (void)(^^void);
    (void)(^^bool);
    (void)(^^char);
    (void)(^^signed char);
    (void)(^^unsigned char);
    (void)(^^short);
    (void)(^^unsigned short);
    (void)(^^int);
    (void)(^^unsigned int);
    (void)(^^long);
    (void)(^^unsigned long);
    (void)(^^long long);
    (void)(^^float);
    (void)(^^double);
    (void)(^^const void);
    (void)(^^decltype(nullptr));

    (void)(^^::);
    constexpr auto x = 1;
    (void)(^^x);
    (void)(^^A);
    (void)(^^B);
    (void)(^^f);
}
