// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that virtual thunks whose function types change between the vtable
// prototype and the actual function definition are properly emitted into the
// module. This exercises the thunk replacement path in maybeEmitThunk where the
// vtable type (from getFunctionTypeForVTable) differs from the actual function
// type (from arrangeGlobalDeclaration), causing the old thunk to be erased and
// a new one created. The new thunk must be properly inserted into the module.

struct Pad { int data[12]; };
struct ForType { int id; };

struct Component {
  virtual ~Component() = default;
  virtual void onInit() {}
};

struct HasPad {
  virtual ~HasPad() = default;
  virtual Pad& getOrCreatePad(ForType ft) = 0;
};

struct NodeBase : virtual Component {
  virtual ~NodeBase() = default;
};

struct Node : NodeBase, HasPad {
  ~Node() override = default;
  Pad& getOrCreatePad(ForType ft) override {
    static Pad p;
    return p;
  }
};

struct SimpleNode : Node {
  ~SimpleNode() override = default;
  Pad& getOrCreatePad(ForType ft) override {
    static Pad p;
    return p;
  }
};

void test() {
  SimpleNode sn;
  ForType ft{42};
  sn.getOrCreatePad(ft);
}

// LLVM: define linkonce_odr noundef ptr @_ZThn8_N10SimpleNode14getOrCreatePadE7ForType(ptr noundef %{{.*}}, i64 %{{.*}})
// LLVM: define linkonce_odr noundef ptr @_ZThn8_N4Node14getOrCreatePadE7ForType(ptr noundef %{{.*}}, i64 %{{.*}})

// OGCG: define linkonce_odr noundef ptr @_ZThn8_N10SimpleNode14getOrCreatePadE7ForType(ptr noundef %{{.*}}, i64 %{{.*}})
// OGCG: define linkonce_odr noundef ptr @_ZThn8_N4Node14getOrCreatePadE7ForType(ptr noundef %{{.*}}, i64 %{{.*}})
