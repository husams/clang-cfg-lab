#!/usr/bin/env python3
"""Generate a C++ translation unit with a large, random call graph (Part 11.8).

    scripts/gen_calls.py --functions 3000 --fanout 3 --seed 1 > out/big.cpp

The output has N prototypes `int f0(int x);` ... `int f<N-1>(int x);`, then one definition per
function

    int f7(int x) { if (x <= 0) return 7; return f2(x - 1) + f9(x - 1) + f41(x - 1); }

and a `main` that calls `f0(5)`. Each function calls K functions picked at random from the
whole set (a function that draws the same callee twice calls it once, so K is an upper bound;
a function may draw itself). The `x - 1` / `x <= 0` guard makes every call terminate, so the
file is a valid program, and the call graph is a random one: it is mostly one big strongly
connected component, which is the shape that stresses SCC code.

--recursive P  adds, with probability P per function, a call to the function itself (default 0:
               only the self calls the random draws happen to produce).

The output depends only on the arguments: the generator is `random.Random(seed)` and nothing
else (no clock, no hash order), so the same arguments give the same bytes on every run.
Standard library only.
"""
import argparse
import random
import sys


def generate(functions: int, fanout: int, seed: int, recursive: float) -> str:
    rng = random.Random(seed)
    out = [f"// generated: {functions} functions"]
    out += [f"int f{i}(int x);" for i in range(functions)]
    for i in range(functions):
        callees = {rng.randrange(functions) for _ in range(fanout)}
        # the extra draw is guarded so that --recursive 0 consumes exactly the same random stream
        if recursive > 0 and rng.random() < recursive:
            callees.add(i)
        calls = " + ".join(f"f{c}(x - 1)" for c in sorted(callees))
        out.append(f"int f{i}(int x) {{ if (x <= 0) return {i}; return {calls}; }}")
    out.append("int main() { return f0(5); }")
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--functions", type=int, default=3000, help="number of functions (default 3000)")
    ap.add_argument("--fanout", type=int, default=3, help="callee draws per function (default 3)")
    ap.add_argument("--seed", type=int, default=1, help="random seed (default 1)")
    ap.add_argument("--recursive", type=float, default=0.0, help="probability of an extra self call (default 0)")
    args = ap.parse_args()
    if args.functions < 1 or args.fanout < 1 or not 0.0 <= args.recursive <= 1.0:
        ap.error("--functions and --fanout must be >= 1 and --recursive in [0, 1]")
    sys.stdout.write(generate(args.functions, args.fanout, args.seed, args.recursive))
    return 0


if __name__ == "__main__":
    sys.exit(main())
