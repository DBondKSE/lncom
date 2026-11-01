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
- Structs and functions are declared only at the top level, structs first, then [mutexes](#mutexes), then functions, then the statements.

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

## Arrays

```
|:::|:|: a {::|:::::@5}
::|::::: n {7}
::::|:|: b {:|::::::@3@n}
::::|:|: flags {::::::|:@2@:::::::|}
a@0 = 1
|:|::::: i {1}
|:|:|:|: i != 5
{
    a@i = a@(i - 1) * 2
    i = i + 1
}
:||:::|: a
:||:::|: b
:||:::|: flags
|||||||| a@3
```

This prints `[1, 2, 4, 8, 16]`, `[7, 7, 7]`, `[true, true]` and `Program exit with result 8`.

An array is a fixed number of values of one built-in type. Its type is written `i32@5`: five `i32`s.

- **An array literal** is a built-in type key, `@` and the size: `::|:::::@50` is 50 `i32` zeros (`false` for `bool`). A second `@` and a factor fill every element with that value: `::|:::::@50@n`. The size is a number from 1 to 4096.
- **An array variable** is declared with the array key alone, and its element type and size come from its initial value: an array literal, another array (`::::|:|: c {a}` copies `a`), or a call that returns one. The top bit of the array key decides whether the elements can be assigned: `|:::|:|:` (138) declares a mutable array. An element type takes a plain key (`|:|:::::@4` is an error).
- **A field, a parameter or a result** writes the whole type: the array key, `@`, the element type, `@` and the size, as in `:||::::: total (::::|:|:@::|:::::@5 xs) :|::::::` or the field `|:::|:|:@:|::::::@9 cells`. A parameter takes the plain key, like every parameter.
- **`a@i` reads an element**, and `a@i = v` writes one. The index stands after the whole chain (`p.cells@2`, `f()@0`) and is a number, a variable or chain, or an expression in parentheses: `a@(i - 1)`. It must be an `i32` or an `i64`. Writing an element needs the same mutability as writing a field: a mutable variable, and every field on the way mutable.
- **An index out of range** is a compile error when it is a number: `index 4 is out of range for an array of 4`. Otherwise it is checked when the program runs: `runtime error: line 6:11: index 9 is out of range for an array of 4` on stderr, exit code 1.
- **An array is copied** when it is assigned (`a = b`, same element type and size), passed to a function, returned, or passed to a thread, just as an object is. A function that changes its own copy never changes the caller's array.
- `print` writes an array as `[1, 2, 3]`, also inside an object: `Board{marks: [false, true, true], size: 3}`. Arrays cannot be compared or used in arithmetic.

## Threads

A `spawn` block (`:||:::||`) runs each of its lines on its own thread, and its `}` waits for all of them. Two threads sum the halves of 1..1000:

```
:||::::: sum (::|::::: from, ::|::::: to) :|::::::
{
    ||:::::: s {0}
    |:|::::: i {from}
    |:|:|:|: i != to + 1
    {
        s = s + i
        i = i + 1
    }
    |||||||| s
}
:||:::||
{
    :|:::::: a {sum(1, 500)}
    :|:::::: b {sum(501, 1000)}
}
:||:::|: a
:||:::|: b
|||||||| (a + b)
```

This prints `125250`, `375250` and `Program exit with result 500500`.

A **range line** `@count f(...)` starts `count` threads that call the same function, and `@` alone as an argument is each thread's index, 0 to `count - 1`. With `4` on the standard input, four threads add their thousands to one total under a mutex:

```
::::||:: m
:||::::: part (::|::::: k, ::|::::: size) ::::::::
{
    ||:::::: s {0}
    |:|::::: i {k * size + 1}
    |:|:|:|: i != (k + 1) * size + 1
    {
        s = s + i
        i = i + 1
    }
    ::||||:: m
    {
        total = total + s
    }
}
||:::::: total {0}
::|::::: count {:||::::|}
:||:::||
{
    @count part(@, 1000)
}
:||:::|: total
|||||||| total
```

This prints `8002000` and `Program exit with result 8002000`, the sum of 1..4000.

### Spawn blocks

- A spawn line is a call of a function of the program (`f(1)`), a declaration whose value is one such call (`:|:::::: a {sum(1, 500)}`), or a range line. A member function, an assignment, `print` or a block cannot be a spawn line, and a spawn block has at least one line.
- The arguments are evaluated on the spawning thread, left to right and line by line, and copied into a structure that the new thread receives. Objects and arrays are copied too, so a thread never shares a parameter with anyone.
- The block's `}` waits for every thread of the block, in the order of the lines. Only then do the variables of its declarations get their values, so they can be used after the block but not inside it: `::|::::: b {f(a)}` after `a {f(1)}` in the same block is an error. These variables belong to the block that holds the spawn block, even at the top level, so functions do not see them.
- A thread may start a spawn block of its own, and may call anything a function can. If any thread stops with a runtime error, the whole program stops.
- If `pthread_create` fails, the program stops with `runtime error: line L:C: cannot start a thread for 'f'`.

### Range lines

- `@count f(args)`: `count` is a factor (a number, a variable, a call or an expression in parentheses) of an integer type, and the call has at least one argument that is `@` alone. `@` inside a larger expression (`f(@ + 1)`) or outside a range line is an error.
- The other arguments are evaluated once, before any thread starts, and every thread receives a copy of the same values.
- A number as the count must be from 1 to 1024. A count known only at run time starts no threads if it is 0 or less, and stops the program with `runtime error: line L:C: a range line starts at most 1024 threads, got N` if it is more than 1024.
- The value the function returns, if any, is dropped: results come back through globals written under a lock.
- A range line does the same work as a loop that calls `f(0)`, `f(1)`, ... in turn, but the calls run at the same time, and in no fixed order.

### Mutexes

- A mutex is declared at the top level, after the structs and before the functions, with the mutex key (`::::||::`) and a name. It has no value, is ready before the first statement runs, and every function can use it. Mutexes have their own namespace, and a name is declared once.
- `::||||:: m` followed by a block is a lock block: it locks `m`, runs the block, and unlocks `m` at its `}`, and also before an `exit` that leaves the block from inside it, so a mutex is never left locked.
- Locking a mutex that is already held waits forever, so it is an error: directly (`mutex 'm' is already locked here, locking it again would wait forever`), or through a call, at any depth, of a function that locks it, including a function whose spawned threads lock it (`'inner' locks mutex 'm', which is already locked here, it would wait forever`). Two different mutexes locked in opposite orders by two threads are not detected.

### What happens to a variable two threads write

Threads share nothing but the global variables: parameters are copies, each thread has its own locals, and the variables of the spawning code are not visible to functions. So a race can only happen on a global, and the compiler checks every spawn block for one:

- A global is **shared** by a spawn block if a thread of the block writes it (an assignment to it, to its field or element, or a call of a mutating member function on it) and another thread of the same block reads or writes it. The threads of a range line count as several threads, so a global that a range line's function writes is always shared. A thread includes everything its function calls, at any depth.
- Every use of a shared global inside those threads must hold a mutex, and all of them must hold the same one. It counts as held when the use stands in a lock block, or when every call that leads to the use, inside that thread, is made from inside one.
- Otherwise the program does not compile: `global 'total' is written by a thread of the spawn block at line 7:1 while another thread uses it, use it inside a lock`, or `... use global 'total' under different mutexes, they need one in common`.

Without the check, the generated code would load `total`, add to it, and store it back with ordinary loads and stores, so two threads could both load the old value and one addition would be lost. With the check, a program that compiles always gets the same final value, however the threads interleave. Code outside the threads needs no lock: the spawning thread waits at the `}`, and the join makes every write of the threads visible after it.

Output is a different matter: `print` from several threads is allowed, and each line comes out whole, but the order of lines from different threads is not fixed. `read` from threads is allowed too, and which thread gets which input is not fixed either.

### The IR of a spawn block

Each spawned function gets a structure for its arguments (and its result, if it has one) and a small function that a thread starts in. For the range line `@n add(@, 10)`, where `add (::|::::: k, ::|::::: step)` returns nothing, the compiler produces:

```llvm
%add.args = type { i32, i32 }

start.body:                                       ; preds = %start.cond
  %slot = getelementptr inbounds %add.args, ptr %add.args, i64 %i
  %k = getelementptr inbounds nuw %add.args, ptr %slot, i32 0, i32 0
  %index = trunc i64 %i to i32
  store i32 %index, ptr %k, align 4
  %step = getelementptr inbounds nuw %add.args, ptr %slot, i32 0, i32 1
  store i32 10, ptr %step, align 4
  %tid = getelementptr inbounds ptr, ptr %add.tids, i64 %i
  %rc = call i32 @pthread_create(ptr %tid, ptr null, ptr @add.thread, ptr %slot)

define internal ptr @add.thread(ptr %args) {
entry:
  %k = getelementptr inbounds nuw %add.args, ptr %args, i32 0, i32 0
  %k1 = load i32, ptr %k, align 4
  %step = getelementptr inbounds nuw %add.args, ptr %args, i32 0, i32 1
  %step2 = load i32, ptr %step, align 4
  call void @fn_add(i32 %k1, i32 %step2)
  ret ptr null
}
```

`%add.args` is the structure the arguments travel in: the range line `malloc`s one per thread, and the loop body `start.body` fills slot `%i` with the thread index (`trunc` of the loop counter to the `i32` parameter `k`) and the shared argument `10`, then calls `pthread_create` with the slot and the address where the thread id goes. `@add.thread` is the function the thread starts in: it has the `void *(void *)` shape `pthread_create` needs, loads the parameters back out of the structure, and calls the ordinary `@fn_add`, storing the result into the structure's last field when the function has one. After the loop, a second loop over the same ids calls `pthread_join` for each thread and then `free`s both arrays, which is the `}` of the spawn block. A plain spawn line does the same with one structure on the stack, and its declaration loads the result from that structure after the join.

## The optimizer

`--optimize` runs three transformations on the checked tree, before code generation. Each one keeps everything the program prints, reads and exits with. The proof is `tests/opt`: each program comes with the tree after `--optimize --ast` and with its output, which must be the same with and without the flag.

### Unused variables

- A declaration nobody reads is removed, together with every assignment to it, wherever it stands: at the top level, in `if`, `while` and lock blocks, in function and member function bodies, and as a declaration in a spawn block (if its call has no effect, see below).
- Reads are counted per declaration, through the declaration the semantic pass resolved for each name, so an inner `x` that shadows an outer one keeps only itself alive. The target of an assignment is not a read; the start of a field chain, an indexed array and the object of a member call are.
- A removed initial value may hold the only read of another variable, so the pass repeats until a round removes nothing.
- A declaration whose initial value contains `read` or a call of a function with an effect stays, and so does an assignment whose value or index contains one, together with its variable. Parameters and fields stay.

A function has an **effect** if its body prints, reads, has a `while` (it may never return), starts threads, locks a mutex, assigns a global, or calls a mutating member function on a global; if it is a mutating member function; if it calls itself, directly or through others; or if it calls a function with an effect. A removed value may also lose a runtime error: `::|::::: x {a * b}` that nobody reads is removed even if `a * b` would overflow.

### Unused functions

- A function of the program that nobody calls is removed with its whole body. A call counts wherever it stands: in the statements, in another function, in a member function, or as a spawn line.
- Removing a body, or a declaration whose value called the function, may leave another function uncalled, so this runs in the same loop as the unused variables, until a round removes neither.
- Member functions are never removed.

### Inlining

A function called exactly once is replaced at its call site by its body:

- Each parameter becomes a constant declaration initialised with its argument, in order; objects and arrays are copied, as for a call.
- The body's statements follow, with every local declaration renamed `name#N`, where `N` counts every renaming in the program. `#` cannot appear in an identifier, so the new names never collide with the caller's.
- The value of the body's final `exit` stands where the call stood. For a call standing alone, a function's value goes into a declaration of its own, which the unused-variable pass removes if nothing in it has an effect.
- The function then has no calls left and is removed as unused. The pass inlines one function at a time and runs the removals again after each, so a function called once by a function that was just inlined is inlined next.

Inlined shapes: the call is the whole value of a declaration, an assignment, a call statement, a `print`, an `exit`, or an `if` condition, and the function is a function of the program whose only `exit` is the last statement of its body (or that has no `exit`).

Left alone:
- a function that leaves through `exit` inside an `if`, a `while` or a lock block;
- a recursive function, directly or through other functions;
- a function that runs on a thread: its body must stay a function for `pthread_create`;
- a member function;
- a call inside a larger expression (`f(x) + 1`), because moving the body in front of the statement could reorder it with the rest of the expression;
- a call in a `while` condition, which runs on every turn of the loop;
- a call in a spawn line or a range line's count.

`tests/opt/inline_value.txt` inlines `area (::|::::: w, ::|::::: h)`, whose body is `||:::::: s {w * h}` and `|||||||| (s + 1)`, at `:|:::::: total {area(s, 3)}`. Before, `main` calls the function and stores what it returns:

```llvm
  %s = load i32, ptr @g_s, align 4
  %area = call i64 @fn_area(i32 %s, i32 3)
  store i64 %area, ptr @g_total, align 4
```

After, `@fn_area` is gone, and its parameters and local are `main`'s own slots, named after the renamed declarations:

```llvm
  %"w#1" = alloca i32, align 4
  %"h#2" = alloca i32, align 4
  %"s#3" = alloca i64, align 8
  ...
  %s = load i32, ptr @g_s, align 4
  store i32 %s, ptr %"w#1", align 4
  store i32 3, ptr %"h#2", align 4
  %"w#11" = load i32, ptr %"w#1", align 4
  %"h#22" = load i32, ptr %"h#2", align 4
  %1 = call { i32, i1 } @llvm.smul.with.overflow.i32(i32 %"w#11", i32 %"h#22")
  ...
  store i64 %wide, ptr %"s#3", align 4
  %"s#33" = load i64, ptr %"s#3", align 4
  %3 = call { i64, i1 } @llvm.sadd.with.overflow.i64(i64 %"s#33", i64 1)
  ...
  store i64 %value4, ptr @g_total, align 4
```

The cloned nodes keep their original positions, so an overflow in the inlined body still reports the line of `area`.

## The real program

[programs/matrix.txt](programs/matrix.txt) is a matrix workshop. It asks for two 3x3 matrices of whole numbers, A and B, nine numbers each, row by row, and then for commands until `0`: `1` multiplies A by B and keeps the product in A, `2` shows A row by row, `3` prints the trace of A, and `4` prints the sums of A and B. At the end it prints how many multiplications were done and exits with the trace of A.

- Structure: `Matrix` holds the nine cells in an array field and has the member functions `at`, `show` and `trace`.
- Threads: a multiplication is the spawn block `@3 row(@)`, one thread per row of the product. Each thread reads the global matrices `a` and `b` and writes its row into the global `product` inside `::||||:: guard`; without the lock, the program would not compile. Command `4` is a spawn block with two declarations, `sa {total(a)}` and `sb {total(b)}`, which sum both matrices at once on two threads.
- Functions: `fill` reads nine numbers into an array, `row` computes a row, `total` sums a matrix passed by copy.

To build it into a native binary with the LLVM tools (the same `llc` and `clang` that `llvm-config` points to) and run the two recorded sessions:

```sh
./compiler programs/matrix.txt matrix.ll
llc -filetype=obj matrix.ll -o matrix.o
clang matrix.o -pthread -o matrix
./matrix < programs/matrix.1.in     # output in programs/matrix.1.expected
./matrix < programs/matrix.2.in     # output in programs/matrix.2.expected
```

Session 1 multiplies the identity by B and shows the product; session 2 prints both sums, multiplies twice, shows the result, tries an unknown command and prints the trace. `tests/run_tests.sh` runs both sessions.

## Building

This needs `clang++` with C++17 and LLVM (tested with LLVM 22). On macOS, `brew install llvm` provides it.

```sh
./build.sh
```

This produces the `compiler` binary. The script uses `llvm-config` from the `PATH`, or `/opt/homebrew/opt/llvm/bin/llvm-config` if there is none; set `LLVM_CONFIG` to use another one.

## Usage

```sh
./compiler program.txt output.ll              # compile to LLVM IR
./compiler --optimize program.txt output.ll   # optimize the tree, then compile
./compiler --ast program.txt                  # print the AST
./compiler --ast --optimize program.txt       # print the tree after the optimizer
./compiler --tokens program.txt               # print the tokens
./compiler --check program.txt                # run the semantic checks only
```

Flags may stand anywhere on the command line, before, after or between the paths. `--ast` alone prints the tree as the parser built it; with `--optimize` the program is checked and optimized first, and the tree is printed with the names the optimizer gave.

To build and run the compiled program, pass the IR to `clang` from the same LLVM, with `-pthread` for programs that use threads (`lli output.ll` also runs it directly):

```sh
./compiler program.txt output.ll
/opt/homebrew/opt/llvm/bin/clang output.ll -pthread -o program
./program < input.txt
```

[The real program](#the-real-program) shows the same with `llc` and `clang` as two steps.

The AST is printed one node per line and indented by depth. Each line shows the node's fields (name, type, `mut` or `const`, operator), its role in the parent node, and its `line:column`:

```
Decl total i64 mut [1:1]
  init: Int 10 [1:17]
```

The new nodes show their flags the same way: `Fn fact result i64` with its `param: Param n i32` children, `Field center struct Point mut`, `Call fact args 1` with its `arg:` children, and `Print "text"`. A chain shows how it ends: `Var c.center.x` reads fields, `MemberCall d2 args 0` with an `object:` child ends in a member call, and `FieldOf x` reads a field of a call result.

Stage 3 adds `Array i32@5` with an optional `fill:` child, `Index` with `array:` and `index:` children, and `Assign a@` with an `index:` child before its `value:`. Threads show as `Mutex m`, `Spawn` with its lines as children, `Range` with `count:` and `call:` children, `ThreadIndex` for `@`, and `Lock m` with its `body:`. An array variable shows `Decl a array mut` before the semantic pass and `Decl a i32@5 mut` after it.

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
- An array is an LLVM array type, `[5 x i32]`, stored in an `alloca` or a global like any variable. An element is a `getelementptr` with the index; an index that is not a number is first compared with the size (`icmp uge`, so a negative index is out of range too) and branches to the runtime error. Copies use `llvm.memmove`, a zero array `llvm.memset`, and a filled one a loop. An array parameter is a `ptr` to a copy the caller makes, so the callee never copies it again.
- A mutex `m` is `@mutex_m`, an `internal global [64 x i8]` aligned to 16 (the size of `pthread_mutex_t` on macOS, larger than on Linux), set up with `pthread_mutex_init` at the start of `main`. A lock block calls `pthread_mutex_lock` and `pthread_mutex_unlock`, and an `exit` inside one unlocks every mutex it holds, innermost first, before it returns.
- A spawn block is described [with its IR](#the-ir-of-a-spawn-block). `pthread_create`, `pthread_join`, the mutex functions, `malloc` and `free` are declared and linked from the C library like `printf`.

## Tests

```sh
tests/run_tests.sh
```

The script builds the compiler, runs every test and lists the ones that failed. It exits with a non-zero code if any test fails. It links with `clang` from the LLVM that `llvm-config` points to; set `CLANG` to use another one.

```sh
tests/run_tests.sh --optimize
```

runs every test with `--optimize` as well: each valid program must print the same output, each invalid program must fail with the same error. `.ast` files in `tests/ok` hold the parser's tree, so they are skipped in this mode.

- `tests/ok/*.txt` are valid programs, each with an `.expected` file, an `.ast` file or both, with the same name:
  - `NAME.expected`: the program is compiled to LLVM IR, built with `clang` and run; its standard output must match the file exactly. If `NAME.in` exists, it is the program's standard input.
  - `NAME.stderr`: the program must stop with a non-zero exit code and print exactly this to stderr (a runtime overflow, index or thread-count error).
  - `NAME.ast`: the `--ast` output must match the file.
- `tests/err/*.txt` are invalid programs. Each is compiled with `./compiler NAME.txt out.ll` and must fail with exactly the error line in the `.expected` file with the same name, print nothing to stdout and write no output file.
- `tests/opt/*.txt` test the optimizer, two or more programs per transformation (`unused_var_`, `unused_fn_`, `inline_`). `NAME.ast` is the tree after `--ast --optimize`, and `NAME.expected` the output, which the program must print both when compiled without and with `--optimize`.
- `programs/NAME.SESSION.in` and `programs/NAME.SESSION.expected` are recorded sessions of `programs/NAME.txt`: the program is compiled, given the `.in` file as input, and must print the `.expected` file exactly.

Test names start with the feature they cover: `scope_`, `type_`, `struct_`, `fn_`, `member_`, `global_`, `io_`/`print_`/`read_`/`string_`, `overflow_`, and in Stage 3 `thread_` and `array_`. Each rule in the sections above has a valid program that follows it and an invalid one that breaks it.
