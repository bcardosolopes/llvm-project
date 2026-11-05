// RUN: %clang_cc1 -triple aarch64-linux-gnu -std=c++17 \
// RUN:   -fexperimental-relative-c++-abi-vtables -fvisibility=hidden \
// RUN:   -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -std=c++17 \
// RUN:   -fexperimental-relative-c++-abi-vtables -fvisibility=hidden \
// RUN:   -emit-llvm -o - %s | FileCheck %s

// A linkonce_odr vtable is in a COMDAT group even when hidden visibility makes
// it dso_local. Keep the concrete relative vtable behind a local symbol so its
// initializer and every address point use the same COMDAT-selected definition.
// CHECK-DAG: @_ZTV10SerializerIiE.local = linkonce_odr hidden unnamed_addr constant
// CHECK-DAG: @_ZL5value = internal global {{.*}} @_ZTV10SerializerIiE.local
// CHECK-DAG: @_ZTV10SerializerIiE = linkonce_odr hidden unnamed_addr alias {{.*}} @_ZTV10SerializerIiE.local
// CHECK-DAG: @_ZTV6Strong = hidden unnamed_addr constant
// CHECK-NOT: @_ZTV6Strong.local

template <typename T>
struct Serializer {
  virtual ~Serializer() = default;
  virtual int protocol() const { return sizeof(T); }
};

const Serializer<int> value;

extern "C" const Serializer<int>* getSerializer() {
  return &value;
}

struct Strong {
  virtual ~Strong();
  virtual int protocol() const;
};

Strong::~Strong() = default;
int Strong::protocol() const {
  return 1;
}

const Strong strongValue;

extern "C" const Strong* getStrong() {
  return &strongValue;
}
