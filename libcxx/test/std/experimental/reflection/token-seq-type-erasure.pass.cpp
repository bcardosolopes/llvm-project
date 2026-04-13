//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection

// RUN: %{build}
// RUN: %{exec} %t.exe

#include <meta>
#include <format>
#include <cassert>

#include "test_macros.h"
#include "assert_macros.h"

using std::meta::id;

consteval auto interface_functions_of(std::meta::info ty) -> std::vector<std::meta::info> {
    auto v = members_of(ty, std::meta::access_context::current());
    std::erase_if(v, [](std::meta::info m){
        return not is_function(m) or is_special_member_function(m) or is_static_member(m);
    });
    return v;
}

consteval auto param_tokens(std::vector<std::meta::info> params,
                            std::string_view  name_prefix = "")
    -> std::meta::token_sequence
{
  auto result = std::meta::list_builder(^^{ , });
  for (int k = 0; std::meta::info p : params) {
    if (is_function_parameter(p)) p = type_of(p);
    if (not name_prefix.empty()) {
      result += ^^{ \(p) \(id(name_prefix, k++)) };
    } else {
      result += ^^{ \(p) };
    }
  }

  report_tokens("param_tokens", result);
  return result;
}

consteval auto inject_Vtable(std::meta::info interface) -> void {
  auto vtable_members = std::meta::list_builder();
  for (std::meta::info mem : interface_functions_of(interface)) {
    std::meta::info  r = return_type_of(mem);
    auto name = identifier_of(mem);
    auto params = std::meta::list_builder(^^{ , });
    params += is_const(type_of(mem)) ? ^^{ void const* } : ^^{ void* };
    params += param_tokens(parameters_of(mem));
    vtable_members += ^^{
      \(r) (*\(id(name)))(\(params));
    };
  }

  report_tokens("vtable", vtable_members);

  queue_injection(^^{
    struct VTable {
      \(vtable_members)
    } const *vtable;
  });
}

consteval auto inject_vtable_for(std::meta::info interface) -> void {
  auto inits = std::meta::list_builder(^^{ , });
  for (std::meta::info mem : interface_functions_of(interface)) {
    std::meta::info r = return_type_of(mem);
    auto name = identifier_of(mem);
    std::meta::list_builder params(^^{ , }), args(^^{ , });
    params += is_const(type_of(mem)) ? ^^{ void const* obj } : ^^{ void* obj };
    params += param_tokens(parameters_of(mem), "p");
    std::meta::token_sequence cast_type = is_const(type_of(mem)) ? ^^{ T const* } : ^^{ T* };
    for (int k = 0; std::meta::info _ : parameters_of(mem)) {
      args += ^^{ \(id("p", k++)) };
    }

    inits += ^^{
      +[](\(params))-> \(r) {
        return static_cast<\(cast_type)>(obj)->\(id(name))( \(args) );
      }
    };
  }

  queue_injection(^^{
    template <class T> static inline constexpr VTable vtable_for = {
      \(inits)
    };
  });
}


consteval auto inject_interface(std::meta::info interface) -> void {
  auto forwarders = std::meta::list_builder();
  for (std::meta::info mem : interface_functions_of(interface)) {
    std::meta::info r = return_type_of(mem);
    auto name = id(identifier_of(mem));
    auto param_list = parameters_of(mem);
    std::meta::list_builder params(^^{ , }), args(^^{ , });
    params += param_tokens(param_list, "p");
    args += ^^{ data };
    for (int k = 0; k < param_list.size(); ++k) {
      args += ^^{ \(id("p", k)) };
    }
    auto suffix = is_const(type_of(mem)) ? ^^{ const } : ^^{ };

    forwarders += ^^{
      auto \(name)(\(params)) \(suffix) -> \(r) {
        return vtable->\(name)(\(args));
      }
    };
  }

  report_tokens("forwarders", forwarders);
  queue_injection(forwarders);
}

consteval auto inject_erasing_ctor() -> void {
  queue_injection(^^{
    template <class T> Dyn(T&& t)
      : data(&t)
      , vtable(&vtable_for<std::remove_cvref_t<T>>)
      {}
  });
}

template<class Iface> class Dyn {
  void *data;
  consteval {
    inject_Vtable(^^Iface);
  }
  consteval {
    inject_vtable_for(^^Iface);
  }
public:
  consteval {
    inject_interface(^^Iface);
  }

  consteval {
    inject_erasing_ctor();
  }
  Dyn(Dyn&) = default;
  Dyn(Dyn const&) = default;
};

struct Interface {
    auto get() const -> int;
};

struct Constant {
    auto get() const -> int { return 1; };
};

struct Variable {
    int i;
    auto get() const -> int { return i; };
};

int main() {
    auto c = Constant();
    auto v1 = Variable{10};
    auto v2 = Variable{20};

    auto stuff = std::vector<Dyn<Interface>>{c, v1, v2};
    int const sum = stuff[0].get() + stuff[1].get() + stuff[2].get();
    assert(sum == 31);
}
