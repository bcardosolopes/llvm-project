// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that TLS wrapper functions are generated for extern thread_local
// variables (not defined in this TU) and for template-instantiated TLS vars.
// Previously, CIR only created wrapper declarations (not definitions) for
// TLS variables not defined in this TU, because getOrCreateCIRGlobal was
// not called during the wrapper code path.

extern thread_local int ext_tls;
int &get_ext_ref() { return ext_tls; }

// The extern global must exist as a declaration.
// CHECK-DAG: @ext_tls = external thread_local global i32

// The wrapper must have a body (not just a declare).
// Extern vars with no definition in this TU get linkonce_odr linkage
// (discardable since the defining TU provides a non-discardable copy).
// CHECK-DAG: define linkonce_odr hidden noundef ptr @_ZTW7ext_tls() {{#[0-9]+}} comdat

// Also test template-instantiated TLS variables.
template<typename T> struct TLS_Templ { static thread_local T val; };
template<typename T> thread_local T TLS_Templ<T>::val{};

int use_template() { return TLS_Templ<int>::val; }

// The template instantiation must have a wrapper with a body.
// CHECK-DAG: define weak_odr hidden noundef ptr @_ZTWN9TLS_TemplIiE3valE() {{#[0-9]+}} comdat

// Static member declared but not defined in this TU.
struct HasTLS { static thread_local int n; };
int use_member() { return HasTLS::n; }

// Must have wrapper with body (even though member is only declared).
// No definition → linkonce_odr (same as ext_tls).
// CHECK-DAG: define linkonce_odr hidden noundef ptr @_ZTWN6HasTLS1nE() {{#[0-9]+}} comdat
