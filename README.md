# lncom

A small programming language where every keyword is an 8-bit binary key: `:` is a 0 bit and `|` is a 1 bit. This repository holds its compiler, written in C++: a lexer and a parser that turn a program into an Abstract Syntax Tree, a semantic pass that checks it, and a code generator that emits LLVM IR.

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
| `::::::::` | 00000000 | 0 | `false`; as a function's result type, "returns nothing" |
| `:::::::\|` | 00000001 | 1 | `true` |
| `::::::\|:` | 00000010 | 2 | `bool` |
| `:::::\|\|:` | 00000110 | 6 | `struct` |
| `::::\|\|\|\|` | 00001111 | 15 | `else` |
| `::\|:::::` | 00100000 | 32 | `i32` |
| `:\|::::::` | 01000000 | 64 | `i64` |
| `:\|\|:::::` | 01100000 | 96 | `fn` |
| `:\|\|::::\|` | 01100001 | 97 | `read` |
| `:\|\|:::\|:` | 01100010 | 98 | `print` |
| `\|:::::\|:` | 10000010 | 130 | `bool` mutable |
| `\|::::\|\|:` | 10000110 | 134 | `struct` mutable |
| `\|:\|:::::` | 10100000 | 160 | `i32` mutable |
| `\|:\|:\|:\|:` | 10101010 | 170 | `while` |
| `\|\|::::::` | 11000000 | 192 | `i64` mutable |
| `\|\|\|\|::::` | 11110000 | 240 | `if` |
| `\|\|\|\|\|\|\|\|` | 11111111 | 255 | `exit` |

Any other key, a key with fewer or more than 8 bits, or a key glued to a letter or a digit is a lexical error.

`struct` (0000 0110) and `fn` (0110 0000) are the same pattern in the low and the high half of the key: data and code. `read` and `print` are the built-in functions, so their keys are `fn` + 1 and `fn` + 2.

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

**exit.** A program ends with `exit`, which takes one factor: a number, a variable, a field chain, a call, `read`, a negation or a parenthesised expression. A block may also end with `exit`. At the top level `exit` ends the program and prints `Program exit with result <value>` and a newline; the value is an `i32`, an `i64` or a `bool` (printed as `true` or `false`), and the program's exit code is 0.

```
|||||||| (a)
```

**Comments.** They run from `//` to the end of the line. Blank lines are ignored.

### Scopes and types

These Stage 1 rules are now checked by the semantic pass:

- A variable is declared before it is used, and a name is declared once per scope. A block opens a new scope: a declaration inside it may shadow an outer name with any type, and the block's names are gone after its `}`.
- A constant (declared with a plain type key) cannot be assigned.
- An integer literal is an `i32` if it is at most 2147483647, otherwise an `i64`. A literal larger than the largest i64 (9223372036854775807) is a lexical error, and a literal that does not fit its target is a compile error: `::|::::: x {3000000000}` fails with `constant 3000000000 does not fit in i32`.
- An `i32` value widens to `i64` wherever an `i64` is expected (an initial value, an assignment, an argument, a result). An `i64` never narrows to `i32`.
- `+`, `-` and `*` take two integers. The result is an `i64` if either side is an `i64`, otherwise an `i32`.
- `==` and `!=` compare two integers (of any widths) or two bools, and give a `bool`. Objects cannot be compared.
- `!` takes a `bool`. The condition of `if` and `while` is a `bool`.
- If `+`, `-` or `*` overflows its result type at run time, the program prints `runtime error: line L:C: i32 overflow in '+'` (with the operator's position, its type and the operator) to stderr and stops with exit code 1, without the exit line.

## Structures

```
:::::||: Point
{
    |:|::::: x
    |:|::::: y
}
:::::||: Circle
{
    |::::||: Point center
    ::|::::: r
}
|::::||: Circle c {{1, 2}, 5}
c.center.x = 10
:||:::|: c
|||||||| c.r
```

This prints `Circle{center: Point{x: 10, y: 2}, r: 5}` and `Program exit with result 5`.

A struct is declared with the struct key, its name, and a body of fields followed by [member functions](#member-functions). A field is a type key and a name; a struct type is written as the struct key and the struct's name. Mutability is set per field and per object with the top bit, never on the struct itself: `:::::||: Point p {..}` is a constant object, `|::::||: Point p {..}` a mutable one.

- A struct name is declared once, and it cannot be `i32`, `i64` or `bool`. A struct has at least one field, and its field names are distinct.
- A field's type is a built-in type or a struct declared above it, so a struct never contains itself.
- An object is created with one value per field, in field order, each of the field's type (an `i32` value widens into an `i64` field). A struct field takes a nested `{...}` or an object of that struct. A single object of the same struct copies it: `:::::||: Point q {p}`. Too many or too few values is an error.
- `.` reads a field by name, also through a chain (`c.center.x`) and on a call result (`origin().x`). Naming a field the struct does not have is an error.
- Assigning through a chain needs every step to be mutable: the variable is declared with a mutable key, and every field on the way is mutable. `c.center.x = 10` above needs `c`, `center` and `x` to be mutable. A parameter is never mutable, and a call result is a temporary copy that cannot be assigned.
- Assigning a whole object (`p = q`) replaces every field, so every field of the struct, at every depth, must be mutable.
- Objects cannot be compared or used in arithmetic.
- An object passed to a function or returned from one is copied.

## Functions

```
:||::::: fact (::|::::: n) :|::::::
{
    ||||:::: n == 0
    {
        |||||||| 1
    }
    |||||||| (n * fact(n - 1))
}
:||::::: greet () ::::::::
{
    :||:::|: "hello"
}
greet()
|||||||| fact(10)
```

This prints `hello` and `Program exit with result 3628800`.

A function is the fn key, its name, its parameters in parentheses and its result type. The result type `::::::::` means the function returns nothing. Inside a function, `exit` leaves the function.

- A function name is declared once. Variables, functions and structs have separate namespaces: a name followed by `(` names a function, a name after the struct key names a struct.
- Parameter types and the result type are built-in types or declared structs. The parameter names of one function are distinct.
- A call passes exactly as many arguments as the function has parameters, and each argument has its parameter's type (an `i32` widens into an `i64` parameter). Too many arguments, too few, or an argument of the wrong type is an error. Arguments are evaluated left to right.
- In a function with a result type, the body's last statement is an `exit`, and every `exit` in the body carries a value of the result type (an `i32` widens into an `i64` result).
- In a function that returns nothing, `exit` has no value and the body may end without one. A call to such a function has no value: it can only stand alone as a statement.
- A call that returns a value can also stand alone as a statement; its result is discarded.
- A function may call itself and any function of the program, including one declared below it.
- Parameters are constants holding copies of the arguments.
- A function body sees its parameters, its own variables, every function and struct of the program, and the [global variables](#global-variables). A name is looked up among the body's scopes, innermost first (the parameters form the outermost one), then among the global variables.
- Structs and functions are declared only at the top level, structs first, then functions, then the statements.

## Member functions

```
:::::||: Counter
{
    |:|::::: n

    :||::::: add (::|::::: k) ::::::::
    {
        n = n + k
    }

    :||::::: get () ::|:::::
    {
        |||||||| n
    }
}
|::::||: Counter c {0}
c.add(5)
c.add(2)
|||||||| c.get()
```

This prints `Program exit with result 7`.

- Fields come first, then member functions. A member function's name differs from every field and every other member function of its struct.
- A member function is called on an object, `obj.f(...)`, where the object is a variable, a field chain (`c.center.reset()`) or a call result (`p.moved(1).d2()`).
- Inside a member function a name is looked up among the parameters and local variables first, then among the fields of the object, then among the global variables. The first match wins: a parameter hides a field of the same name, and a field hides a global.
- A call `f(...)` inside a member function looks among the member functions of the same struct first, calling it on the same object, then among the functions of the program.
- The object is passed by address, so a member function that assigns a field changes the caller's object.
- A member function is **mutating** if it assigns a field of its object, or calls a mutating member function on its object or on one of its fields. This is worked out after all bodies are checked, so member functions that call each other are fine. Assigning a global variable does not make a member function mutating.
- A mutating member function may be called only through a chain that is mutable at every step: the variable is declared with a mutable key and every field on the way is mutable. A parameter is not mutable. A call result is a temporary copy: any member function can be called on it, and its changes are lost.

## Global variables

```
:||::::: tick () ::::::::
{
    calls = calls + 1
}
|:|::::: calls {0}
tick()
tick()
|||||||| calls
```

This prints `Program exit with result 2`.

- A global variable is a variable declared at the top level of the program, outside any block. A variable declared inside a top-level `if` or `while` block is local to that block and is not a global.
- Every function and member function may read a global, and assign it if it was declared with a mutable key, even though the functions stand above the globals in the program.
- A function uses a global if its body names it or calls a function that uses it, at any depth. A call may run only after every global its function uses has been declared: a top-level statement may call a function only if every global the function uses is declared by an earlier statement. A global's own initial value runs before its declaration completes, so `::|::::: g {f()}` fails if `f` uses `g`. Moving the first `tick()` above `|:|::::: calls {0}` in the example gives `compilation error: line 5:1: call to 'tick' uses global 'calls' before its declaration at line 6:1`.
- A constant global cannot be assigned, and a mutating member function cannot be called on a constant global object, from any function.

## Input and output

```
::|::::: a {:||::::|}
::|::::: b {:||::::|}
:||:::|: "sum:"
:||:::|: a + b
:||:::|: a == b
|||||||| 0
```

With `3 4` on the standard input this prints `sum:`, `7`, `false` and `Program exit with result 0`, each on its own line.

- `read` (`:||::::|`) reads one decimal integer from the standard input. Its value is an `i32`, it stands wherever a value can, and it takes no parentheses. If no integer can be read, its value is 0.
- `print` (`:||:::|:`) is a statement that takes one value or one string literal and prints it followed by one newline:
  - an `i32` or `i64` as a decimal number;
  - a `bool` as `true` or `false`;
  - a string literal as its bytes. A string literal is `"`, any bytes except `"` and a newline, then `"`; there are no escape sequences, and a string stands only after `print`;
  - an object as its struct's name and its fields in order, `Point{x: 3, y: 4}`. A struct field is printed the same way inside its parent, `Circle{center: Point{x: 1, y: 2}, r: 5}`, and adds no newline of its own.
- Printing a call to a function that returns nothing is an error.
- The output of `print` appears in execution order, before the `Program exit with result` line.

## Building

This needs `clang++` with C++17 and LLVM (tested with LLVM 22). On macOS, `brew install llvm` provides it.

```sh
./build.sh
```

This produces the `compiler` binary. The script uses `llvm-config` from the `PATH`, or `/opt/homebrew/opt/llvm/bin/llvm-config` if there is none; set `LLVM_CONFIG` to use another one.

## Usage

```sh
./compiler program.txt output.ll   # compile to LLVM IR
./compiler --ast program.txt       # print the AST
./compiler --tokens program.txt    # print the tokens
./compiler --check program.txt     # run the semantic checks only
```

To build and run the compiled program, pass the IR to `clang` from the same LLVM (`lli output.ll` also runs it directly):

```sh
./compiler program.txt output.ll
/opt/homebrew/opt/llvm/bin/clang output.ll -o program
./program < input.txt
```

The AST is printed one node per line and indented by depth. Each line shows the node's fields (name, type, `mut` or `const`, operator), its role in the parent node, and its `line:column`:

```
Decl total i64 mut [1:1]
  init: Int 10 [1:17]
```

The new nodes show their flags the same way: `Fn fact result i64` with its `param: Param n i32` children, `Field center struct Point mut`, `Call fact args 1` with its `arg:` children, and `Print "text"`. A chain shows how it ends: `Var c.center.x` reads fields, `MemberCall d2 args 0` with an `object:` child ends in a member call, and `FieldOf x` reads a field of a call result.

On a lexical, syntax or semantic error, the compiler prints one line to stderr, prints nothing to stdout, writes no output file and exits with code 1:

```
compilation error: line 1:1: key '::|::::' has 7 bits, a key needs exactly 8
```

### Generated IR

- A struct is an identified type, `%Point = type { i32, i32 }`, and an object is an `alloca` of it. A field is reached with a `getelementptr` by its index, which the semantic pass looks up by name.
- A function `name` becomes `@fn_name`, and a member function `d2` of `Point` becomes `@Point.d2(ptr %this, ...)`, so neither clashes with `main` or the C library. Every function is declared before any body is generated.
- A global variable `x` becomes `@g_x`, an `internal global` set to zero; its initial value is stored in `main` where its declaration stands.
- `printf`, `scanf`, `dprintf` and `exit` are declared without bodies and linked from the C library. Formats and texts are global constants.
- `+`, `-` and `*` use `llvm.sadd/ssub/smul.with.overflow`; on overflow the program branches to a block that prints the runtime error with `dprintf` to stderr and calls `exit(1)`.

## Tests

```sh
tests/run_tests.sh
```

The script builds the compiler, runs every test and lists the ones that failed. It exits with a non-zero code if any test fails. It links with `clang` from the LLVM that `llvm-config` points to; set `CLANG` to use another one.

- `tests/ok/*.txt` are valid programs, each with an `.expected` file, an `.ast` file or both, with the same name:
  - `NAME.expected`: the program is compiled to LLVM IR, built with `clang` and run; its standard output must match the file exactly. If `NAME.in` exists, it is the program's standard input.
  - `NAME.stderr`: the program must stop with a non-zero exit code and print exactly this to stderr (a runtime overflow error).
  - `NAME.ast`: the `--ast` output must match the file.
- `tests/err/*.txt` are invalid programs. Each is compiled with `./compiler NAME.txt out.ll` and must fail with exactly the error line in the `.expected` file with the same name, print nothing to stdout and write no output file.

Test names start with the feature they cover: `scope_`, `type_`, `struct_`, `fn_`, `member_`, `global_`, `io_`/`print_`/`read_`/`string_`, and `overflow_`. Each rule in the sections above has a valid program that follows it and an invalid one that breaks it.
