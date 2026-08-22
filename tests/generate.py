#!/usr/bin/env python3
"""Write the load suite into tests/load/.

The cases in tests/cases are small on purpose: each one asks whether a rule
of the language is obeyed. These ask a different question - whether the
compiler still obeys them when the program is large - and the answers are
not the same. Two defects were found by the first version of this file and
neither could have been reached by a small program:

  * an arm64 frame past 4095 bytes, because 'sub sp, sp, #n' takes twelve
    bits and the assembler refuses the rest;
  * a call with more arguments than the registers can carry, which read past
    the end of the register table and gave a five-argument function %xmm0
    for its fifth argument on Windows.

Every program here prints something a person could check by hand, and the
suite compares it against the app's interpreter like any other case. Sizes
are chosen so the interpreter can run each in well under a second: this is a
test of the compiler at scale, not a benchmark of the oracle.

Deterministic - regenerating produces byte-identical files, so the committed
programs can be diffed after a change to this script.

    ./tests/generate.py
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "load")


def write(name, header, body):
    text = "".join("// %s\n" % line for line in header.strip().split("\n"))
    with open(os.path.join(OUT, name + ".shm"), "w") as f:
        f.write(text + body)


def frame(n, name):
    """Many locals at once, which is what makes a frame large."""
    locals_ = "\n".join("  int v%d : %d" % (i, i % 97) for i in range(n))
    sums = "\n".join("  t : t + v%d" % i for i in range(n))
    return "fun <> = main() {\n  int t : 0\n%s\n%s\n  ? t\n}\n" % (locals_, sums)


def main():
    if not os.path.isdir(OUT):
        os.mkdir(OUT)

    write("frame", """
A frame of about five kilobytes. On arm64 the reservation no longer fits the
twelve-bit immediate of 'sub sp, sp, #n', which is where this started.
""", frame(600, "frame"))

    write("frame_huge", """
Two and a half thousand locals: twenty kilobytes of frame, which puts the
slots at the top of it past the reach of a scaled load - 4095 times the
access width, so 16380 for a word. The address has to be formed in a
register instead.
""", frame(2500, "frame_huge"))

    terms = 300
    write("deep_expression", """
One expression with three hundred terms. Each left operand waits in a slot
while the right is evaluated, so this is really a question about how many
slots the generator counted.
""", "fun <> = main() {\n  ? %s\n}\n" %
        ("1" + "".join(" + %d" % i for i in range(1, terms))))

    depth = 150
    write("nested_parens", """
A hundred and fifty parentheses deep, which is a question about the parser's
own recursion rather than the generated code's.
""", "fun <> = main() {\n  ? %s\n}\n" % ("(" * depth + "7" + ")" * depth))

    depth = 40
    write("nested_loops", """
Forty loops inside each other, each running once. Every loop holds four
hidden slots and hands out three labels, so this counts both.
""", "fun <> = main() {\n  int t : 0\n" +
        "".join("  for i%d : 1 to 1 {\n" % i for i in range(depth)) +
        "  t : t + 1\n" + "}\n" * depth + "  ? t\n}\n")

    # Twenty, not more, and the limit is the oracle's rather than this
    # compiler's. The app's parser is exponential in the depth of nested
    # calls - 24 takes it nine seconds and 26 does not finish - while shc
    # compiles 30 without pausing. Recorded here because the number looks
    # arbitrary otherwise.
    depth = 20
    write("nested_calls", """
A call as the argument of a call, twenty deep. The evaluation slots nest with
the calls, which is the case that first made the generator's count and the
checker's estimate disagree.
""", "fun <int> = f(n: int) {\n  return n + 1\n}\nfun <> = main() {\n  ? %s\n}\n" %
        ("f(" * depth + "0" + ")" * depth))

    n = 200
    write("if_chain", """
Two hundred elseif branches in one function: a label apiece, and a jump from
each to the same end.
""", "fun <int> = pick(n: int) {\n  if n = 0 {\n    return 0\n  }" +
        "".join(" elseif n = %d {\n    return %d\n  }" % (i, i * i)
                for i in range(1, n)) +
        " else {\n    return -1\n  }\n}\n"
        "fun <> = main() {\n  ? pick(7) pick(150) pick(999)\n}\n")

    n = 400
    write("many_functions", """
Four hundred functions, all called. The runtime counts recursion depth per
function in a table of a thousand, so this is also a question about that.
""", "\n".join("fun <int> = f%d(n: int) { return n + %d }" % (i, i % 13)
               for i in range(n)) +
        "\nfun <> = main() {\n  int t : 0\n" +
        "".join("  t : t + f%d(1)\n" % i for i in range(n)) + "  ? t\n}\n")

    n = 500
    write("many_globals", """
Five hundred globals. They live in one block addressed by an offset, and the
offsets at the far end of it are past the reach of a scaled load.
""", "\n".join("int g%d : %d" % (i, i % 89) for i in range(n)) +
        "\nfun <> = main() {\n  int t : 0\n" +
        "".join("  t : t + g%d\n" % i for i in range(n)) + "  ? t\n}\n")

    n = 300
    write("many_strings", """
Three hundred distinct string literals, each a byte array in the module's
read-only data and each a fresh array at run time.
""", "fun <> = main() {\n" +
        "".join('  ?? "s%d"\n' % i for i in range(n)) + "  ? 0\n}\n")

    write("long_string", """
A literal of two thousand characters, which is a byte list of two thousand
entries in the assembly.
""", 'fun <> = main() {\n  ? "%s"\n}\n' % ("abcdefghij" * 200))

    n = 1500
    write("long_program", """
Fifteen hundred statements in one function. Nothing here is difficult; there
is simply a lot of it, and every statement costs a line number call.
""", "fun <> = main() {\n  int t : 0\n" +
        "".join("  t : t + %d\n" % (i % 7) for i in range(n)) + "  ? t\n}\n")

    write("arguments", """
More arguments than the registers can carry, in every shape the language can
write one: plain, mixed with reals, by reference, and a function with four
outputs. Microsoft's convention carries four and System V's six, so a
five-argument function already overflows on one of the three - and used to be
handed a register that was not in the table.
""", """fun <int> = five(a: int, b: int, c: int, d: int, e2: int) {
  return a + b*10 + c*100 + d*1000 + e2*10000
}

fun <real> = ten(a: int, x: real, b: int, y: real, c: int, z: real,
                 d: int, w: real, e2: int, v: real) {
  return real(a + b + c + d + e2) + x + y + z + w + v
}

fun <int,int,int,int> = four(n: int) {
  return (n, n*2, n*3, n*4)
}

fun <> = bump5(&a: int, &b: int, &c: int, &d: int, &e2: int) {
  a : a + 1
  b : b + 2
  c : c + 3
  d : d + 4
  e2 : e2 + 5
}

fun <int> = twelve(a: int, b: int, c: int, d: int, e2: int, f: int,
                   g: int, h: int, i: int, j: int, k: int, l: int) {
  return a+b+c+d+e2+f+g+h+i+j+k+l
}

fun <> = fillrow(m[][]: real, r: int, a: real, b: real, c: real,
                 d: real, e2: real) {
  m[r][0] : a
  m[r][1] : b
  m[r][2] : c
  m[r][3] : d
  m[r][4] : e2
}

fun <> = main() {
  ? five(1,2,3,4,5)
  ? ten(1, 0.5, 2, 0.25, 3, 0.125, 4, 0.0625, 5, 0.03125)
  <p,q,r,s> : four(3)
  ? p q r s
  int a : 10
  int b : 20
  int c : 30
  int d : 40
  int e2 : 50
  bump5(a,b,c,d,e2)
  ? a b c d e2
  ? twelve(1,2,3,4,5,6,7,8,9,10,11,12)
  real m[2][5]
  fillrow(m, 0, 1.0, 2.0, 3.0, 4.0, 5.0)
  fillrow(m, 1, 6.0, 7.0, 8.0, 9.0, 10.0)
  ? m
}
""")

    write("rank5", """
A rank-five array: the runtime builds four levels of references to reach the
numbers, and .dim(n) walks back down them.
""", """fun <> = main() {
  int a[3][4][5][6][7]
  ? a.row a.col a.dim(2) a.dim(3) a.dim(4) a.dim(5)
  a[2][3][4][5][6] : 42
  a[0][0][0][0][0] : 7
  ? a[2][3][4][5][6] a[0][0][0][0][0] a[1][1][1][1][1]
  ? len(a)
}
""")

    write("matrix", """
A sixteen by sixteen matrix multiplied by another, by reference, and summed.
Every element costs sixteen multiplies and sixteen adds through the runtime,
so this is the closest thing here to a measurement.
""", """fun <> = mul(a[][]: real, b[][]: real, c[][]: real) {
  real s : 0.0
  for i < a.row {
    for j < b.col {
      s : 0.0
      for k < a.col {
        s : s + a[i][k] * b[k][j]
      }
      c[i][j] : s
    }
  }
}

fun <> = main() {
  real a[16][16]
  real b[16][16]
  real c[16][16]
  for i < a.row {
    for j < a.col {
      a[i][j] : real(i + j)
      b[i][j] : real(i - j)
    }
  }
  mul(a, b, c)
  real t : 0.0
  for i < c.row {
    for j < c.col {
      t : t + c[i][j]
    }
  }
  ? t
  ? c[0][0] c[15][15]
}
""")

    write("sort", """
Two hundred numbers put in order the long way. Nothing but indexing, and a
great deal of it.
""", """fun <> = sort(v[]: int) {
  int t : 0
  for i < v.row {
    for j < v.row - i - 1 {
      if v[j] > v[j+1] {
        t : v[j]
        v[j] : v[j+1]
        v[j+1] : t
      }
    }
  }
}

fun <> = main() {
  int v[200]
  int seed : 1
  for i < v.row {
    seed : (seed * 31 + 17) % 1009
    v[i] : seed
  }
  sort(v)
  ? v[0] v[99] v[199]
  int ordered : 1
  for i : 1 to v.row - 1 {
    if v[i-1] > v[i] {
      ordered : 0
    }
  }
  ? ordered
}
""")

    write("recursion_mutual", """
Two functions calling each other a hundred deep, which the overall ceiling of
1024 frames has to let through and the per-function one of 128 must not stop.
""", """fun <int> = even(n: int) {
  if n = 0 {
    return 1
  }
  return odd(n - 1)
}

fun <int> = odd(n: int) {
  if n = 0 {
    return 0
  }
  return even(n - 1)
}

fun <> = main() {
  ? even(100) odd(100) even(101) odd(101)
}
""")

    write("text_building", """
A string built a piece at a time in a loop, joined, compared and indexed -
every text operation the language has, run enough times to matter.
""", """fun <> = main() {
  char s[64] : ""
  for i < 20 {
    s +: "ab"
  }
  ? s
  ? len(s)
  char t[64] : ""
  t +: s
  ? t = s
  ? t < "b"
  char u[8] : "abc"
  ? u + "def"
  int n : 0
  for i < s.row {
    if s[i] = char(97) {
      n : n + 1
    }
  }
  ? n
}
""")

    write("real_math", """
Every built-in over a range of arguments, printed at full precision. This is
where a target that got an argument register wrong would show it, because
each of these is a call with one or two reals.
""", """fun <> = main() {
  real x : 0.0
  ? prec(12)
  for i : 1 to 8 {
    x : real(i) / 4.0
    ? x sqrt(x) log(x) exp(x)
    ? sin(x) cos(x) tan(x)
    ? asin(x/4.0) acos(x/4.0) atan(x)
    ? hypot(x, 2.0) atan2(x, 2.0) pow(x, 2.5)
    ? round(x) ceil(x) floor(x) trunc(x)
    ? abs(0.0-x) max(x, 1.0) min(x, 1.0)
  }
  ? pi e
  ? abs(0-7) max(3, 9) min(3, 9)
}
""")

    print("wrote %d programs to tests/load" %
          len([f for f in os.listdir(OUT) if f.endswith(".shm")]))


main()
