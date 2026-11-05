// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-cir \
// RUN:   -std=gnu++20 -w %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that self-referential function pointer members in template classes
// get correct types after record layout completion. The function pointer
// type contains a parameter that refers back to the enclosing template class,
// which creates a circular dependency during type conversion.

template<typename T, unsigned long N = static_cast<unsigned long>(-1)>
struct span {
    T* data_;
    unsigned long size_;
    constexpr span(T* d, unsigned long s) : data_(d), size_(s) {}
    unsigned long size() const { return size_; }
};

template <typename T>
struct Pyramid {
    using InitFn = void (*)(span<Pyramid<T>* const>, int, void*, void*);
    InitFn initFn_ = nullptr;
    int data_ = 0;

    Pyramid(InitFn fn = nullptr) : initFn_(fn) {
        if (initFn_ == nullptr) {
            initFn_ = defaultInit;
        }
    }

    void run() {
        Pyramid<T>* self = this;
        span<Pyramid<T>* const> s(&self, 1);
        initFn_(s, 0, nullptr, nullptr);
    }

    static void defaultInit(span<Pyramid<T>* const>, int, void*, void*) {}
};

// CHECK-LABEL: cir.func
// Verify the function pointer member has the correct type (not !cir.func<()>)
// CHECK: cir.load
// CHECK-NOT: error
void test() {
    Pyramid<unsigned char> p;
    p.run();
}
