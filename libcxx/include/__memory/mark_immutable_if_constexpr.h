//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_MARK_IMMUTABLE_IF_CONSTEXPR_H
#define _LIBCPP___MEMORY_MARK_IMMUTABLE_IF_CONSTEXPR_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__builtin_mark_immutable_if_constexpr)

// P4341 ext (non-transient constexpr allocation): during the hypothetical
// constant destruction of a constexpr variable, asserts that the allocation
// starting at __p has been immutable since the end of the variable's
// initialization, permitting destruction-time reads and later constant reads
// of its contents. In every other context — including at runtime, during
// ordinary constant evaluation, and when __p does not point to the start of a
// surviving allocation (e.g. an SSO buffer) — this is a no-op.
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr void mark_immutable_if_constexpr(_Tp* __p) _NOEXCEPT {
  __builtin_mark_immutable_if_constexpr(
      const_cast<void*>(static_cast<const void*>(__p)));
}

#endif // __has_builtin(__builtin_mark_immutable_if_constexpr)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_MARK_IMMUTABLE_IF_CONSTEXPR_H
