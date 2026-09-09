#ifndef _LIBCPP___FORMAT_TEMPLATE_STRING_H
#define _LIBCPP___FORMAT_TEMPLATE_STRING_H

#include <__concepts/convertible_to.h>
#include <__format/buffer.h>
#include <__format/format_args.h>
#include <__format/format_context.h>
#include <__format/format_functions.h>
#include <__format/formatter.h>
#include <__format/formatter_output.h>
#include <__format/parser_std_format_spec.h>
#include <__format/template_string_fwd.h>
#include <__utility/move.h>
#include <string_view>
#if _LIBCPP_HAS_LOCALIZATION
#  include <__locale>
#  include <optional>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_HAS_TEMPLATE_STRINGS

// rebind_format: the adaptor every template string transformer ends with --
// pair a runtime pattern with the arguments of an existing template string.
// The pattern is stored exactly as passed: a std::string transfers
// ownership, a string_view or pointer references storage the caller keeps
// alive. The result is necessarily runtime-checked: only a consteval fmt()
// (i.e. a literal's) can satisfy format_string's compile-time checking, so
// a runtime pattern travels as a dynamic format string.
// Rebinding a bound pattern replaces it (declared here, befriended below):
// the invariant that a rebound_format pairs a pattern directly with a
// template string always holds, and types do not nest.
template <class _Pattern, class _P0, template_string _Sp>
  requires convertible_to<const _Pattern&, string_view>
constexpr auto rebind_format(_Pattern __pattern,
                             rebound_format<_P0, _Sp> __s);

template <class _Pattern, template_string _Sp>
class rebound_format {
  _Pattern __pattern_;
  _Sp __s_;

  template <class _Pattern2, class _P0, template_string _Sp2>
    requires convertible_to<const _Pattern2&, string_view>
  friend constexpr auto rebind_format(_Pattern2, rebound_format<_P0, _Sp2>);

public:
  _LIBCPP_HIDE_FROM_ABI constexpr rebound_format(_Pattern __pattern, _Sp __s)
      : __pattern_(std::move(__pattern)), __s_(std::move(__s)) {}

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr string_view pattern() const {
    return string_view(__pattern_);
  }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI auto fmt() const {
    return std::dynamic_format(pattern());
  }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI decltype(auto) exprs() const {
    return __s_.exprs();
  }
};

template <template_string _Sp, class _Pattern>
  requires convertible_to<const _Pattern&, string_view>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto
rebind_format(_Pattern __pattern, _Sp __s) {
  return rebound_format<_Pattern, _Sp>(std::move(__pattern), std::move(__s));
}

template <class _Pattern, class _P0, template_string _Sp>
  requires convertible_to<const _Pattern&, string_view>
constexpr auto rebind_format(_Pattern __pattern,
                             rebound_format<_P0, _Sp> __s) {
  return rebound_format<_Pattern, _Sp>(std::move(__pattern),
                                       std::move(__s.__s_));
}

// Formatting a template string (or a rebound_format) as a format argument:
//   std::print("[{}] {}", level, t"{n} files removed");
// renders it exactly as std::format(s) would, directly into the output, so
// embedding one message in another costs no intermediate string. This is a
// deliberate divergence from Python's template strings, whose str() does not
// render; here std::print(t"...") already renders, so "{}" doing the same is
// the consistent choice. The format-spec is fill-and-align and width;
// padding needs the rendered width, so only then is the text buffered first.

// Formats __fmt with __args into __out_it, under the enclosing context's
// locale when it has one.
template <class _OutIt, class _FormatContext, class... _Args>
_LIBCPP_HIDE_FROM_ABI _OutIt
__vformat_parts(_OutIt __out_it, _FormatContext& __ctx, string_view __fmt, _Args&... __args) {
  auto __store = std::make_format_args(__args...);
#  if _LIBCPP_HAS_LOCALIZATION
  if (auto __loc = __ctx.__locale_opt())
    return std::vformat_to(std::move(__out_it), *std::move(__loc), __fmt, __store);
#  else
  (void)__ctx;
#  endif
  return std::vformat_to(std::move(__out_it), __fmt, __store);
}

template <class _Sp>
struct __formatter_deferred_format {
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return __parser_.__parse(__ctx, __format_spec::__fields_fill_align_width);
  }

  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
  __format(string_view __fmt, const _Sp& __s, _FormatContext& __ctx) const {
    __format_spec::__parsed_specifications<char> __specs = __parser_.__get_parsed_std_specifications(__ctx);
    auto& [...__parts] = __s.exprs();
    if (!__specs.__has_width())
      return std::__vformat_parts(__ctx.out(), __ctx, __fmt, __parts...);

    __format::__allocating_buffer<char> __buffer;
    std::__vformat_parts(__buffer.__make_output_iterator(), __ctx, __fmt, __parts...);
    return __formatter::__write_string_no_precision(__buffer.__view(), __ctx.out(), __specs);
  }

  __format_spec::__parser<char> __parser_{.__alignment_ = __format_spec::__alignment::__left};
};

template <template_string _Sp>
struct formatter<_Sp, char> : __formatter_deferred_format<_Sp> {
  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(const _Sp& __s, _FormatContext& __ctx) const {
    // The literal's own format string is checked against its argument types
    // at compile time here, exactly as std::format(s) does.
    auto& [...__parts] = __s.exprs();
    [[maybe_unused]] constexpr format_string<decltype(__parts)...> __checked = _Sp::fmt();
    return this->__format(__checked.get(), __s, __ctx);
  }
};

template <class _Pattern, template_string _Sp>
struct formatter<rebound_format<_Pattern, _Sp>, char> : __formatter_deferred_format<rebound_format<_Pattern, _Sp>> {
  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
  format(const rebound_format<_Pattern, _Sp>& __s, _FormatContext& __ctx) const {
    return this->__format(__s.pattern(), __s, __ctx);
  }
};

template <__deferred_format _S>
[[nodiscard]] _LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI string
format(_S&& __s) {
    auto& [...__parts] = __s.exprs();
    return std::format(__s.fmt(), __parts...);
}

template <output_iterator<const char&> _OutIt, __deferred_format _S>
_LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI _OutIt
format_to(_OutIt __out_it, _S&& __s) {
    auto& [...__parts] = __s.exprs();
    return std::format_to(std::move(__out_it), __s.fmt(), __parts...);
}

// A literal operator template taking a parameter is only valid with the
// language feature enabled.
inline namespace literals {
inline namespace string_literals {

template <__deferred_format _S>
inline _LIBCPP_HIDE_FROM_ABI string
operator""s(_S&& __s) {
    auto& [...__parts] = __s.exprs();
    return std::format(__s.fmt(), __parts...);
}

}
}


#endif // _LIBCPP_HAS_TEMPLATE_STRINGS

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS


#endif
