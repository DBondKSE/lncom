# lncom

A small programming language where every keyword is an 8-bit binary key: `:` is a 0 bit and `|` is a 1 bit. This repository holds its lexer and parser, written in C++, which turn a program into an Abstract Syntax Tree.

```
// sum of 1..10
|:|::::: i {0}
|:|::::: sum {0}
|:|:|:|: i != 10
{
    i = i + 1
    sum = sum + i
}
|||||||| sum
```

## Keys

A key is exactly 8 bits, read left to right from the most significant bit: `:|::::::` is `01000000` = 64 = `i64`.

| Key | Binary | Value | Keyword |
|---|---|---|---|
| `::::::::` | 00000000 | 0 | `false` |
| `:::::::\|` | 00000001 | 1 | `true` |
| `::::::\|:` | 00000010 | 2 | `bool` |
| `::::\|\|\|\|` | 00001111 | 15 | `else` |
| `::\|:::::` | 00100000 | 32 | `i32` |
| `:\|::::::` | 01000000 | 64 | `i64` |
| `\|:::::\|:` | 10000010 | 130 | `bool` mutable |
| `\|:\|:::::` | 10100000 | 160 | `i32` mutable |
| `\|:\|:\|:\|:` | 10101010 | 170 | `while` |
| `\|\|::::::` | 11000000 | 192 | `i64` mutable |
| `\|\|\|\|::::` | 11110000 | 240 | `if` |
| `\|\|\|\|\|\|\|\|` | 11111111 | 255 | `exit` |

Any other key, a key with fewer or more than 8 bits, or a key glued to a letter or a digit is a lexical error.

## The language

The full grammar, with comments on each rule, is in [grammar.ebnf](grammar.ebnf).

**Types.** `i32`, `i64` and `bool`.

```
::|::::: small {42}
:|:::::: big {5000000000}
::::::|: flag {:::::::|}
```

**Constants and mutable variables.** A declaration always has an initial value in `{}`. A plain type key declares a constant. Setting the type key's top bit (value 128) declares a mutable variable, so there is no separate `mut` keyword.

```
::|::::: c {1}      // i32, constant
|:|::::: m {1}      // i32, mutable: 32 + 128 = 160
```

**Assignment.** It uses `=`.

```
||:::::: total {10}
total = total * 2
```

**Operators.** These are `+`, `-`, `*`, the comparisons `==` and `!=`, and the prefix `!`. `*` binds tighter than `+` and `-`, and parentheses override precedence. An expression may hold only one comparison, so chain comparisons with parentheses.

```
::|::::: a {(1 + 2) * 3}
::::::|: r {(a == 9) != !::::::::}
```

**if / else.** The condition goes on the `if` line, and `{`, `}` and `else` each stand alone on their own lines. A block needs at least one statement, and `else` is optional.

```
||||:::: x != 0
{
    x = 0
}
::::||||
{
    x = 1
}
```

**while.** It repeats its block while the condition holds.

```
|:|:|:|: i != 3
{
    i = i + 1
}
```

**exit.** A program ends with `exit`, which takes a number, a variable, a negation or a parenthesised expression. A block may also end with `exit`.

```
|||||||| (a)
```

**Comments.** They run from `//` to the end of the line. Blank lines are ignored.

**Overflow.** A literal larger than the largest i64 (9223372036854775807) is a lexical error. The remaining rules, such as an i32 literal out of range, no narrowing from i64 to i32, and arithmetic overflow at run time, are part of the language and are checked in later stages.

## Building

This needs `clang++` with C++17.

```sh
./build.sh
```

This produces the `compiler` binary.

## Usage

```sh
./compiler --ast program.txt      # print the AST
./compiler --tokens program.txt   # print the tokens
```

The AST is printed one node per line and indented by depth. Each line shows the node's fields (name, type, `mut` or `const`, operator), its role in the parent node, and its `line:column`:

```
Decl total i64 mut [1:1]
  init: Int 10 [1:17]
```

On a lexical or syntax error, the compiler prints one line to stderr, prints nothing to stdout and exits with code 1:

```
compilation error: line 1:1: key '::|::::' has 7 bits, a key needs exactly 8
```

## Tests

```sh
tests/run_tests.sh
```

The script builds the compiler, runs every test and lists the ones that failed. It exits with a non-zero code if any test fails.

- `tests/ok/*.txt` are valid programs. Each program's `--ast` output must match the `.ast` file with the same name.
- `tests/err/*.txt` are invalid programs. Each must fail with exactly the error line in the `.expected` file with the same name and print nothing to stdout.
