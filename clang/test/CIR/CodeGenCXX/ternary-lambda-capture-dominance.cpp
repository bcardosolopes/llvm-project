// RUN: %clang_cc1 -fclangir -emit-obj -fexceptions %s -o /dev/null

// Previously crashed with "operand #0 does not dominate this use".
// Fixed by using CleanupDeactivationScope in VisitLambdaExpr to deactivate
// capture cleanup before outer scope references it.

struct Wrapper {
  template <class F>
  Wrapper(F) {}
  ~Wrapper() {}
};

void consume(Wrapper);

void trigger(Wrapper w) {
  consume(true ? Wrapper(0) : [w] {});
}
