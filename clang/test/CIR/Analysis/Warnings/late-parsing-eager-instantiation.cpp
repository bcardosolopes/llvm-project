// RUN: %clang_cc1 -Wno-error=return-type -std=c++14 -verify=expected,cfg %s
// RUN: %clang_cc1 -fclangir -emit-cir -fclangir-analysis-warnings=return-type -Wno-error=return-type -std=c++14 -verify=expected,cir %s -o /dev/null

// Copy of clang/test/SemaTemplate/late-parsing-eager-instantiation.cpp, also run with
// -fclangir-analysis-warnings=return-type. Diagnostics only one engine
// reports use the cfg- or cir- prefix, with a TODO for ClangIR.

// pr33561
class ArrayBuffer;
template <typename T> class Trans_NS_WTF_RefPtr {
public:
  ArrayBuffer *operator->() { return nullptr; }
};
Trans_NS_WTF_RefPtr<ArrayBuffer> get();
template <typename _Visitor>
constexpr void visit(_Visitor __visitor) {
  __visitor(get()); // cfg-note {{in instantiation}}
}
// TODO(cir): CIRGen does not emit the unused member function data(), nor the
// lambda instantiated from it.
class ArrayBuffer {
  char data() {
    visit([](auto buffer) -> char { // cfg-note {{in instantiation}}
      buffer->data();
    }); // cfg-warning {{non-void lambda does not return a value}}
  } // cfg-warning {{non-void function does not return a value}}
};

// pr34185
template <typename Promise> struct coroutine_handle {
  Promise &promise() const { return
    *static_cast<Promise *>(nullptr); // expected-warning {{binding dereferenced null}}
  }
};

template <typename Promise> auto GetCurrenPromise() {
  struct Awaiter { // expected-note {{in instantiation}}
    void await_suspend(coroutine_handle<Promise> h) {
      h.promise(); // expected-note {{in instantiation}}
    }
  };
  return Awaiter{};
}

void foo() {
  auto &&p = GetCurrenPromise<int>(); // expected-note {{in instantiation}}
}
