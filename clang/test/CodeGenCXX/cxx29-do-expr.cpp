// RUN: %clang_cc1 -std=c++2d -emit-llvm -triple x86_64-linux-gnu -O0 -disable-O0-optnone -o - %s | FileCheck %s

// Basic do-expression: alloca + value flow through join block.
// CHECK-LABEL: define {{.*}} @_Z5basicv
// CHECK: %doexpr.result = alloca i32
// CHECK: store i32 42, ptr %doexpr.result
// CHECK: br label %doexpr.end
// CHECK: doexpr.end:
// CHECK: load i32, ptr %doexpr.result
int basic() {
  return do { do_return 42; };
}

// Two `do_return` paths converge at the join block.
// CHECK-LABEL: define {{.*}} @_Z11two_returnsi
// CHECK: doexpr.end:
int two_returns(int n) {
  return do {
    if (n) do_return 1;
    do_return 2;
  };
}

// 'return' inside do-expression returns from the enclosing function:
// the -1 path stores into %retval and branches to %return, while the
// do_return path stores into %doexpr.result.
// CHECK-LABEL: define {{.*}} @_Z12outer_returnb
// CHECK: store i32 -1, ptr %retval
// CHECK: store i32 1, ptr %doexpr.result
int outer_return(bool b) {
  int x = do {
    if (!b) return -1;
    do_return 1;
  };
  return x + 100;
}

// 'break' inside do-expression breaks the enclosing loop.
// CHECK-LABEL: define {{.*}} @_Z11outer_breakPKii
int outer_break(const int *p, int n) {
  int total = 0;
  for (int i = 0; i < n; ++i) {
    int v = do {
      if (p[i] < 0) break;
      do_return p[i];
    };
    total += v;
  }
  return total;
}

// Explicit return type drives conversion of operand.
// CHECK-LABEL: define {{.*}} @_Z13explicit_typev
// CHECK: store double 1.000000e+00
double explicit_type() {
  return do -> double { do_return 1; };
}

// 'continue' inside do-expression jumps to the enclosing loop's continue
// target. The do-expression's join block is bypassed entirely.
// CHECK-LABEL: define {{.*}} @_Z14outer_continuePKii
// CHECK: for.inc:
int outer_continue(const int *p, int n) {
  int total = 0;
  for (int i = 0; i < n; ++i) {
    int v = do {
      if (p[i] < 0) continue;
      do_return p[i];
    };
    total += v;
  }
  return total;
}

// Aggregate result: a do-expression yielding a struct constructs directly
// into the function's return slot — no intermediate %doexpr.result alloca.
struct Pair { int a, b; };
// CHECK-LABEL: define {{.*}} @_Z9make_pairv
// CHECK-NOT: %doexpr.result
// CHECK: getelementptr {{.*}} %struct.Pair{{.*}} i32 0, i32 0
// CHECK: store i32 1
// CHECK: getelementptr {{.*}} %struct.Pair{{.*}} i32 0, i32 1
// CHECK: store i32 2
// CHECK: br label %doexpr.end
Pair make_pair() {
  return do { do_return Pair{1, 2}; };
}

// RAII destructor for a body-local variable runs before the do-expression's
// join block (via EmitBranchThroughCleanup).
struct Counter {
  int *p;
  Counter(int *p_) : p(p_) { ++*p; }
  ~Counter();
};
// CHECK-LABEL: define {{.*}} @_Z11raii_returnPi
// CHECK: call {{.*}} @_ZN7CounterC{{[12]}}EPi
// CHECK: store i32 {{.*}} ptr %doexpr.result
// CHECK: call void @_ZN7CounterD{{[12]}}Ev
// CHECK: br label %doexpr.end
int raii_return(int *live) {
  return do {
    Counter c(live);
    do_return *live;
  };
}

// Void do-expression: no result alloca, just the body and join block.
// CHECK-LABEL: define {{.*}} @_Z9void_exprPi
// CHECK-NOT: %doexpr.result
// CHECK: doexpr.end:
void void_expr(int *p) {
  (void)(do -> void {
    *p = 7;
    do_return;
  });
}

// `do_return` of a named local lvalue is move-eligible: the IR contains a
// call to the move constructor (taking T &&), not the copy constructor.
struct Movable {
  int x;
  Movable();
  Movable(const Movable &);
  Movable(Movable &&) noexcept;
  ~Movable();
};
// CHECK-LABEL: define {{.*}} @_Z9move_namev
// CHECK: call {{.*}} @_ZN7MovableC{{[12]}}EOS_
// CHECK-NOT: call {{.*}} @_ZN7MovableC{{[12]}}ERKS_
Movable move_name() {
  return do {
    Movable r;
    do_return r;  // implicit move from named local
  };
}

// A const named local can't be moved → falls back to copy.
// CHECK-LABEL: define {{.*}} @_Z10const_namev
// CHECK: call {{.*}} @_ZN7MovableC{{[12]}}ERKS_
Movable const_name() {
  return do {
    const Movable r;
    do_return r;
  };
}

// Reference-typed do-expression with an lvalue operand: the slot is a
// pointer (`T*`); `do_return` stores the operand's address, and the
// surrounding load yields the bound lvalue.
extern int &lvalue();
// CHECK-LABEL: define {{.*}} @_Z6ref_okv
// CHECK: %doexpr.refresult = alloca ptr
// CHECK: call {{.*}} @_Z6lvaluev
// CHECK: store ptr {{.*}} ptr %doexpr.refresult
// CHECK: br label %doexpr.end
// CHECK: doexpr.end:
// CHECK: load ptr, ptr %doexpr.refresult
int &ref_ok() {
  return do -> int & { do_return lvalue(); };
}

// Reference-typed do-expression used in a prvalue (scalar) context: the slot
// holds a `T*`, so the load must dereference it once to get the `T*` and again
// to get the value. Previously this loaded the pointer slot directly as an
// `int`, producing an invalid cast and crashing CodeGen.
int consume(int x);
// CHECK-LABEL: define {{.*}} @_Z12ref_as_valuev
// CHECK: %doexpr.refresult = alloca ptr
// CHECK: store ptr {{.*}} ptr %doexpr.refresult
// CHECK: doexpr.end:
// CHECK: %[[PTR:.*]] = load ptr, ptr %doexpr.refresult
// CHECK: %[[VAL:.*]] = load i32, ptr %[[PTR]]
// CHECK: call {{.*}} @_Z7consumei(i32 {{.*}} %[[VAL]])
int ref_as_value() {
  return consume(do -> int & { do_return lvalue(); });
}

// Basic do-expression init: declare a variable and return its value.
// CHECK-LABEL: define {{.*}} @_Z10init_basicv
// CHECK: alloca i32
// CHECK: store i32 42
// CHECK: load i32
int init_basic() {
  return do [x = 42] { x };
}

// Multiple declarations: each gets its own alloca.
// CHECK-LABEL: define {{.*}} @_Z13init_multiplev
// CHECK: alloca i32
// CHECK: alloca i32
// CHECK: store i32 1
// CHECK: store i32 2
// CHECK: load i32
// CHECK: load i32
// CHECK: add
int init_multiple() {
  return do [a = 1, b = 2] { a + b };
}

// RAII: init declarations live until the enclosing full-expression.
// CHECK-LABEL: define {{.*}} @_Z12init_raii_doPi
// CHECK: call {{.*}} @_ZN7CounterC{{[12]}}EPi
// CHECK: load i32
// CHECK: call void @_ZN7CounterD{{[12]}}Ev
// CHECK: ret i32
int init_raii_do(int *live) {
  return do [c = Counter(live)] { *live };
}

struct Tracker {
  int value;
  Tracker(int v) : value(v) {}
  ~Tracker();
  int get() const { return value; }
};

Tracker make_tracker(int v);

// The init owns the returned temporary (elided into the capture) and destroys
// it at the end of the enclosing full-expression.
// CHECK-LABEL: define {{.*}} @_Z23init_lifetime_extensionv
// CHECK: call {{.*}} @_Z12make_trackeri
// CHECK: call {{.*}} @_ZNK7Tracker3getEv
// CHECK: call void @_ZN7TrackerD{{[12]}}Ev
// CHECK: ret i32
int init_lifetime_extension() {
  return do [t = make_tracker(42)] { t.get() };
}

// Nested do-expression init: inner variables are destroyed before outer.
// CHECK-LABEL: define {{.*}} @_Z11init_nestedPi
// CHECK: call {{.*}} @_ZN7CounterC{{[12]}}EPi
// CHECK: call {{.*}} @_ZN7CounterC{{[12]}}EPi
// CHECK: call void @_ZN7CounterD{{[12]}}Ev
// CHECK: call void @_ZN7CounterD{{[12]}}Ev
// CHECK: ret i32
int init_nested(int *live) {
  return do [outer = Counter(live)] {
    do_return do [inner = Counter(live)] { *live };
  };
}

int use_value(int v);

// CHECK-LABEL: define {{.*}} @_Z21init_as_subexpressionPi
// CHECK: call {{.*}} @_ZN7CounterC{{[12]}}EPi
// CHECK: load i32
// CHECK: call {{.*}} @_Z9use_valuei
// CHECK: call {{.*}} @_Z9use_valuei
// CHECK: call void @_ZN7CounterD{{[12]}}Ev
// CHECK: ret i32
int init_as_subexpression(int *live) {
  return use_value(do [c = Counter(live)] { *live }) + use_value(100);
}

// CHECK-LABEL: define {{.*}} @_Z34init_lifetime_extend_subexpressionv
// CHECK: call {{.*}} @_Z12make_trackeri
// CHECK: call {{.*}} @_ZNK7Tracker3getEv
// CHECK: call {{.*}} @_Z9use_valuei
// CHECK: call {{.*}} @_Z9use_valuei
// CHECK: call void @_ZN7TrackerD{{[12]}}Ev
// CHECK: ret i32
int init_lifetime_extend_subexpression() {
  return use_value(do [t = make_tracker(42)] { t.get() }) +
         use_value(100);
}
