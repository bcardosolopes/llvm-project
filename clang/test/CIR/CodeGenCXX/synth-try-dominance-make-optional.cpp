// RUN: %clang_cc1 -std=c++17 -triple aarch64-linux-android23 -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-cir %s -o - | FileCheck %s

// Regression test for FaceTrackingDataProviderConfiguration.cpp dominance
// violation: two consecutive sret-returning calls (e.g.
// make_optional) for trivially-destructible members at the same EH cleanup
// depth used to cause "operand #1 does not dominate this use" when the
// second call's destination (get_member) was emitted after the first
// synthetic try and then used inside a shared try body.
//
// The fix hoists pure ops between the TryOp and the
// insertion point before the try when the synthetic try is reused, ensuring
// dominance is maintained.

inline void *operator new(unsigned long, void *p) noexcept { return p; }

template <typename Value, bool TrivDtor = __is_trivially_destructible(Value)>
struct OptStorageBase {
  union {
    char empty;
    Value value;
  };
  bool hasValue;
  constexpr OptStorageBase() : empty(0), hasValue(false) {}
  void clear() { hasValue = false; }
};

template <typename Value>
struct OptStorageBase<Value, false> {
  union {
    char empty;
    Value value;
  };
  bool hasValue;
  constexpr OptStorageBase() : empty(0), hasValue(false) {}
  ~OptStorageBase() {
    if (hasValue) {
      hasValue = false;
      value.~Value();
    }
  }
  OptStorageBase(const OptStorageBase &) = delete;
  OptStorageBase(OptStorageBase &&) = delete;
  OptStorageBase &operator=(const OptStorageBase &) = delete;
  OptStorageBase &operator=(OptStorageBase &&) = delete;
};

template <typename Value>
class Opt : private OptStorageBase<Value> {
public:
  struct PC {};
  using OptStorageBase<Value>::OptStorageBase;

  constexpr Opt() noexcept {}

  Opt(Opt &&src) {
    if (src.hasValue) {
      new (&this->value) Value(static_cast<Value &&>(src.value));
      this->hasValue = true;
      src.hasValue = false;
    }
  }

  template <typename... Args>
  constexpr explicit Opt(PC, Args &&...args) : OptStorageBase<Value>() {
    new (&this->value) Value(static_cast<Args &&>(args)...);
    this->hasValue = true;
  }
};

template <typename T>
Opt<T> make_opt(T v) {
  return Opt<T>(typename Opt<T>::PC{}, static_cast<T &&>(v));
}

struct Ref {
  void *p;
  Ref();
  Ref(const Ref &);
  Ref(Ref &&) noexcept;
  ~Ref();
};

struct BigMap {
  char data[64];
  BigMap();
  BigMap(BigMap &&);
  ~BigMap();
};

enum class Mode { A, B };

// Pattern matching FaceTrackingDataProviderConfiguration: BigMap member
// forces handler init into an invoke (BigMap cleanup active), then two
// consecutive make_opt calls for trivially-destructible members share the
// same EH cleanup depth.
struct S {
  BigMap map;
  Opt<Ref> handler;
  Opt<Mode> mode;
  Opt<unsigned char> version;
  int scalar;

  S(BigMap mp, Opt<Ref> h, Mode m, unsigned char v, int s);
};

S::S(BigMap mp, Opt<Ref> h, Mode m, unsigned char v, int s)
    : map(static_cast<BigMap &&>(mp)),
      handler(static_cast<Opt<Ref> &&>(h)),
      mode(make_opt(m)),
      version(make_opt(v)),
      scalar(s) {}

// Verify that both make_opt calls produce valid CIR (no dominance violation).
// The key invariant: each call's destination (get_member) and argument (load)
// must dominate their use inside the cleanup scope that covers the call.
//
// The member initializers nest two cir.cleanup.scopes: the outer one is opened
// by the BigMap member's destructor, the inner one by the handler's.
//
// CHECK-LABEL: cir.func{{.*}} @_ZN1SC2E6BigMap3OptI3RefE4Modehi
// CHECK: %[[THIS:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!rec_S>>, !cir.ptr<!rec_S>
//
// Every member address is computed up front, ahead of both scopes. They all
// depend only on `this`, so CIRGen's dominance-preserving hoist of pure ops
// lifts them out -- which is precisely the mechanism this test was written to
// guard, and get_member only became eligible for it once it stopped declaring
// a MemRead it never performed. That the addresses now dominate *by
// construction* rather than by careful placement is the point.
//
// BigMap move ctor, then the destructor address the outer scope will use:
// CHECK: cir.get_member %[[THIS]][0] {name = "map"}
// CHECK: cir.call @_ZN6BigMapC1EOS_
// CHECK: %[[MAP:.*]] = cir.get_member %[[THIS]][0] {name = "map"}
// Two handler addresses are emitted, as for map: one for the move ctor and one
// for the destructor the inner cleanup will run. It is the second that the
// cleanup uses.
// CHECK: cir.get_member %[[THIS]][1] {name = "handler"}
// CHECK: %[[HANDLER:.*]] = cir.get_member %[[THIS]][1] {name = "handler"}
// CHECK: cir.get_member %[[THIS]][2] {name = "mode"}
// CHECK: cir.get_member %[[THIS]][3] {name = "version"}
// CHECK: cir.get_member %[[THIS]][4] {name = "scalar"}
// CHECK: cir.cleanup.scope {
//
// Handler move ctor (exception-annotated because the BigMap cleanup is live):
// CHECK:   cir.call exception @_ZN3OptI3RefEC1EOS1_
// CHECK:   cir.cleanup.scope {
//
// Both make_opt calls, in order:
// CHECK:     cir.call exception @{{.*}}make_opt{{.*}}Mode
// CHECK:     cir.call exception @{{.*}}make_opt{{.*}}
//
// Scalar store (nothing throwing left):
// CHECK:     cir.store
// CHECK:     cir.yield
// CHECK:   } cleanup {
// CHECK:     cir.call @_ZN3OptI3RefED1Ev(%[[HANDLER]])
// CHECK:     cir.yield
// CHECK:   }
// CHECK:   cir.yield
// CHECK: } cleanup {
// CHECK:   cir.call @_ZN6BigMapD1Ev(%[[MAP]])
// CHECK:   cir.yield
// CHECK: }
// CHECK: cir.return
