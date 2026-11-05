// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
//
// Regression test: Triple-nested try-catch (3+ levels of nesting) previously
// crashed in FlattenCFG with "empty block: expect at least a terminator"
// because buildLandingPads created empty blocks when callsToRewrite was empty
// (all exception-throwing calls were inside nested TryOps).

int test() {
    try {
        try {
            try {
                throw 1;
            } catch (int) {
                throw; // rethrow to middle try
            }
        } catch (int) {
            throw; // rethrow to outer try
        }
    } catch (int e) {
        return e;
    }
    return 0;
}
