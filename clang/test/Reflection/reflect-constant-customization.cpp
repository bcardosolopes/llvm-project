// RUN: %clang_cc1 -std=c++2d -freflection -verify %s

// P4340 ext: declaration surface for the reflect_constant customization
// point. See reflect-constant.md. This file tests only the declaration-side
// rules: recognized shapes, =default / =delete, access, and the effect on
// is_structural / usability as an NTTP that flows from the structural bit.

namespace std::meta {
  using info = decltype(^^::);
}

// ==== Recognized shapes ====

struct Ok1 {
  consteval auto reflect_constant() const -> std::meta::info;
};
struct Ok2 {
  consteval auto reflect_constant() const& -> std::meta::info;
};
struct Ok3 {
  consteval auto reflect_constant(this Ok3 const&) -> std::meta::info;
};
struct Ok4 {
  consteval auto reflect_constant(this Ok4) -> std::meta::info;
};

// ==== Defaulted and deleted ====

class Defaulted {
  int i;
public:
  consteval auto reflect_constant() const -> std::meta::info = default;
};

struct Deleted {
  int i;
  consteval auto reflect_constant() const -> std::meta::info = delete;
};

class DefaultedOutOfLine {
  int i;
public:
  consteval auto reflect_constant() const -> std::meta::info;
};
consteval auto DefaultedOutOfLine::reflect_constant() const
    -> std::meta::info = default;

// ==== Bad shapes ====

struct NotConsteval {
  constexpr auto reflect_constant() const -> std::meta::info; // expected-error {{'reflect_constant' customization point must be a consteval member function}}
};

struct NotConst {
  consteval auto reflect_constant() -> std::meta::info; // expected-error {{'reflect_constant' customization point must be const-qualified}}
};

struct RValueRef {
  consteval auto reflect_constant() const&& -> std::meta::info; // expected-error {{'reflect_constant' customization point must not be &&-qualified}}
};

struct HasParams {
  consteval auto reflect_constant(int) const -> std::meta::info; // expected-error {{'reflect_constant' customization point must take no parameters}}
};

struct WrongReturn {
  consteval auto reflect_constant() const -> int; // expected-error {{'reflect_constant' customization point must return 'std::meta::info'}}
};

struct TemplateCP {
  template <class T>
  consteval auto reflect_constant() const -> std::meta::info; // expected-error {{'reflect_constant' customization point must not be a template}}
};

class Private {
  consteval auto reflect_constant() const -> std::meta::info; // expected-error {{'reflect_constant' customization point must be public}}
};

struct BadExplicitObject {
  consteval auto reflect_constant(this BadExplicitObject&&) -> std::meta::info; // expected-error {{'reflect_constant' customization point must have an object parameter of type}}
};

struct WrongExplicitObjectType {
  consteval auto reflect_constant(this int) -> std::meta::info; // expected-error {{'reflect_constant' customization point must have an object parameter of type}}
};

// ==== Structural-type effects ====

// __metafunction(6, r) is detail::__metafn_is_structural_type; keep in sync
// with the metafunction table in ExprConstantMeta.cpp.
consteval bool is_structural(std::meta::info r) {
  return __metafunction(6, r);
}

struct NotOtherwiseStructural {
private:
  int i; // private member: not C++26-structural

public:
  consteval auto reflect_constant() const -> std::meta::info;
};
static_assert(is_structural(^^NotOtherwiseStructural));

struct PlainStructural { int i; };
static_assert(is_structural(^^PlainStructural));

static_assert(!is_structural(^^Deleted));

// The opt-out is infectious.
struct HasDeletedMember {
  Deleted d;
};
static_assert(!is_structural(^^HasDeletedMember));

// A customized member makes an otherwise-plain aggregate still structural.
struct HasCustomizedMember {
  NotOtherwiseStructural m;
  int x;
};
static_assert(is_structural(^^HasCustomizedMember));

// Mutable members always disqualify, customization point or not.
struct MutableCustomized {
  mutable int i;
  consteval auto reflect_constant() const -> std::meta::info;
};
static_assert(!is_structural(^^MutableCustomized));
