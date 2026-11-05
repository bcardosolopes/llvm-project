// Second input for ../fat-object-input.cpp. Not a test on its own.
int callee(int x) { return x + 1; }
int helper(int x) { return callee(x); }
