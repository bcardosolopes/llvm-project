// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-arc -fclangir -emit-cir %s -o /dev/null

// Regression test: aggregate conditional emission for non-trivial C structs
// should not trip the stale nonTrivialCStructDestroy assert, and supported
// ObjC-lifetime structs should also make it through auto-var cleanup now.

typedef struct {
  __strong id x;
} NT;

NT make_a(void);
NT make_b(void);

void f(int c) {
  NT t = c ? make_a() : make_b();
  (void)t;
}
