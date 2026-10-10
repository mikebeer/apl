# A Word-Form APL Interpreter, Written in Regina REXX

**File:** `apl.rex`
**Requires:** [Regina REXX](http://regina-rexx.sourceforge.net/) (any recent build; developed and
tested against Regina 3.9.5, 64-bit)
**Run with:**
```
rexx apl.rex               # interactive REPL
rexx apl.rex program.apl   # run a script file
```
(or `regina apl.rex ...` if your `rexx` alias doesn't point at Regina)

---

## 1. Background and design philosophy

This interpreter follows the spirit of Rodnay Zaks' classic description of a
microprogrammed APL implementation: a small set of **primitive functions**,
dispatched either **monadically** (one argument, to the right) or
**dyadically** (two arguments, one on each side), evaluated **right to left**
with **no operator precedence** and no built-in looping construct — exactly
as real APL behaves. Parentheses are the only way to override evaluation
order.

The one deliberate deviation from real APL is notational, not semantic:
APL's glyphs (⍴, ⍳, ⌽, ⍉, ⌈, ⌿, …) aren't part of REXX's ASCII-oriented
source character set, so every primitive is spelled out as a **lower-case
word** instead (`rho`, `iota`, `reverse`, `transpose`, `max`, …). Ordinary
arithmetic keeps its familiar ASCII symbols (`+ - * /`), since those exist on
every keyboard already.

Because the underlying evaluation model — right-to-left, uniform monadic/
dyadic dispatch, flat array values carrying their own shape — is unchanged
from Zaks' description, translating between the "real" glyph-based syntax
and this word-based one is mostly a matter of vocabulary, not of relearning
how APL thinks.

---

## 2. Running a program

There are two ways to use `apl.rex`:

**Interactive REPL** — run it with no arguments to get an immediate-execution
prompt, in the classic APL style:

```
$ rexx apl.rex
APL-in-REXX  --  type )quit to exit
apl> v <- iota 5
apl> v
1 2 3 4 5
apl> reduce plus v
15
apl> )quit
goodbye.
```

Assignments and function definitions persist across the whole session. A
syntax or runtime error prints a message and returns you to the `apl>`
prompt rather than ending the session — only `)quit` (`)exit`, `)off`, or
end-of-input / Ctrl-D) leaves. Type `)help` at any time for a list of
session commands (§9), or `nabla <header>` to define your own function
(§10).

**Script mode** — pass a source file to run it non-interactively:

```
rexx apl.rex myprogram.apl
```

A program file is a plain text file, one **statement per line**. Each line
is either:

- an **assignment**: `name <- expression` (silent — nothing is printed), or
- a bare **expression**: `expression` (the result is printed automatically,
  the way immediate-execution APL echoes untitled results)

Blank lines are ignored. A comment runs from either marker below to the
end of the line: `#`, or the standalone word `lamp` — this dialect's
ASCII spelling of APL's own comment glyph `⍝`, traditionally called
"lamp" (glyph names are pronounceable words in APL culture, the way `∇`
is "del" and `⍴` is "rho"). Either marker is ignored inside a `'quoted
string'`, and `lamp` only counts as a whole word — `clamp` or `lampoon`
are ordinary identifiers, not comments.

```
lamp this whole line is a comment
# so is this, the old-fashioned way
v <- iota 5      lamp this creates a vector and assigns it silently
v                # this prints it: 1 2 3 4 5
```

Since `lamp` triggers a comment wherever it appears as a standalone word,
it can't be used as a variable or function name.

---

## 3. Values: scalars, vectors, and matrices

Every APL value in this dialect is an **array with a shape**:

| Kind    | Shape (rank) | Example                  | Printed as        |
|---------|--------------|---------------------------|--------------------|
| Scalar  | rank 0       | `5`                        | `5`                 |
| Vector  | rank 1       | `1 2 3 4 5`                | `1 2 3 4 5`        |
| Matrix  | rank 2       | `(2 3) rho (1 2 3 4 5 6)`  | two rows, printed one per line, right-aligned |

Numeric **vector literals** are written by simply juxtaposing numbers
separated by spaces — exactly as in real APL:

```
3 -2 5 -7        # a 4-element vector: 3, -2, 5, -7
```

> **Note on minus signs:** a `-` glued directly to a digit (no space) is
> always read as part of a **signed number literal**. To get dyadic
> subtraction, put spaces around the `-`: `5 - 3` is "5 minus 3"; `5 -3` is
> the two-element vector `(5, -3)`.

There is currently **no matrix literal syntax**; build matrices with dyadic
`rho` (reshape) from a flat vector — see §5.

### 3.1 Text values

A quoted string, `'like this'`, is a **text value** — used for messages
and labels rather than as a full character-array type. Double a quote to
include a literal quote: `'it''s here'` prints as `it's here`.

```
'hi mike'              # displays as: hi mike
msg <- 'hi mike'       # store it in a variable
msg                    # displays as: hi mike
```

A bare text value, wherever it appears as a whole statement — including
as a line inside a `nabla`-defined function — displays itself, exactly
like a bare numeric value does. This makes a plain string a convenient
way to leave a message or trace note in a function body:

```
apl> nabla Z<-GREET X
[1]  'hi mike'
[2]  Z<-X*2
[3]  nabla
apl> GREET 5
hi mike
10
```

Text values support **assignment**, **display**, and `cat` between two
text values (plain concatenation, no inserted space). They are not
numeric arrays: passing one to an arithmetic or structural primitive
(`+`, `take`, `reduce`, and so on) is a domain error rather than a silent
misinterpretation:

```
apl> 'hi' + 5
apl: runtime error -- DOMAIN ERROR: + does not accept a text value
```

For explicit output — handy when you want a statement to *always*
display something even where it wouldn't otherwise (see §8's bare
`[]<-`) — text values combine naturally with that shorthand:
`[]<-'starting'`.

---

## 4. Evaluation order (important!)

APL — and this dialect — has **no operator precedence**. Expressions are
evaluated strictly **right to left**:

```
2 + 3 * 4     ->   14      (evaluated right to left: 3*4=12 first, then 2+12)
(2 + 3) * 4   ->   20      (parentheses force the left grouping)
```

A **monadic** function applies to everything to its right:

```
- 5           ->   -5              (monadic minus = negate)
rho iota 5    ->   5               (shape of (iota 5))
reverse iota 5 -> 5 4 3 2 1
```

A **dyadic** function has a value to its left and an expression to its
right:

```
2 take iota 5   ->  1 2
(2 3) rho iota 6
```

---

## 5. Primitive functions

### 5.1 Structural / shape primitives

| Word          | Monadic (one arg, to the right)                              | Dyadic (left arg `Y`, right arg `X`)                                  |
|---------------|----------------------------------------------------------------|--------------------------------------------------------------------------|
| `rho`         | **shape**: `rho X` → vector of X's dimension sizes             | **reshape**: `Y rho X` → reshape/refill X's ravel into shape `Y` (cycles the data if it's too short) |
| `iota`        | **index generator**: `iota N` → vector starting at `[]IO` (default `1`), e.g. `1 2 … N` | — (monadic only)                                        |
| `ravel`       | flatten to a vector, preserving element order                  | — (monadic only)                                                        |
| `reverse`     | reverse a vector; reverse *each row* of a matrix                | — (see `rotate` for the dyadic form)                                    |
| `rotate`      | —                                                                | `N rotate X`: rotate a vector left by `N` (negative = right); for a matrix, `N` (scalar) rotates every row |
| `transpose`   | swap the two axes of a matrix (identity on vectors/scalars)      | — (monadic only)                                                        |
| `take`        | —                                                                | `N take X`: first/last `N` elements of a vector (0-padded if `N` exceeds length); for a matrix, `N` is a 2-element vector `(rows cols)` |
| `drop`        | —                                                                | `N drop X`: drop `N` from the front/back; matrix form takes a 2-vector, symmetric to `take` |
| `cat`         | —                                                                | `X cat Y`: concatenate two vectors; or stack two matrices with the same column count as extra rows |
| `compress`    | —                                                                | `B compress X`: keep the elements (or matrix rows) of `X` where the boolean vector `B` is 1 |
| `index`       | —                                                                | `X index I`: vector element `I`; matrix row `I` (`I` scalar); or matrix element `(row col)` (`I` a 2-vector); or `X index (i1 i2 …)` — a gather of several vector elements at once. Subscripts are interpreted relative to `[]IO` (see §8). |
| `matmul`      | —                                                                | `X matmul Y`: dot product for two vectors; standard matrix multiplication for two matrices |

### 5.2 Arithmetic / scalar primitives (all elementwise, with scalar broadcasting)

| Symbol / word | Monadic                | Dyadic                     |
|---------------|--------------------------|-------------------------------|
| `+`           | identity (`+X` = `X`)     | addition                      |
| `-` / `neg`   | negate                    | subtraction                   |
| `*`           | —                         | multiplication                |
| `/`           | —                         | division (errors on ÷0)       |
| `abs`         | absolute value            | —                              |
| `floor`       | round toward −∞           | —                              |
| `ceil`        | round toward +∞           | —                              |
| `not`         | logical NOT (`0`→`1`, else `0`) | —                        |
| `=  <  >  <=  >=  <>` | —                | elementwise comparison, result is `0`/`1` |
| `max` `min`   | —                         | elementwise max / min          |
| `and` `or`    | —                         | logical AND / OR (operands assumed `0`/`1`) |
| `mod`         | —                         | remainder (`X mod Y`)          |

**Scalar broadcasting:** if either dyadic operand is a scalar, it is applied
to every element of the other operand. If both are arrays, they must have
identical shapes.

### 5.3 Operators (higher-order, prefix form)

Because this dialect writes function *names* as plain words rather than
single glyphs, the reduce/scan/each/outer-product operators are written
**prefix**, naming the function first:

```
reduce <funcname> <expr>       # fold the whole array with <funcname>
scan   <funcname> <expr>       # cumulative fold (vector only)
each   <funcname> <expr>       # apply a monadic function to every element
outer  <funcname> <A> <B>      # outer-product table between vectors A, B
```

`<funcname>` may be any of the dyadic scalar primitives above (`max`, `min`,
`and`, `or`, `mod`, the comparisons, …) plus four **word aliases** for the
arithmetic symbols, needed because a bare `+`/`-`/`*`/`/` can't stand alone
as a "value" the way a function name can:

| Word     | Same as |
|----------|---------|
| `plus`   | `+`     |
| `minus`  | `-`     |
| `times`  | `*`     |
| `divide` | `/`     |

Examples:

```
reduce plus (iota 5)          -> 15            (1+2+3+4+5, right-to-left fold)
scan  plus (1 2 3 4)          -> 1 3 6 10       (running total)
reduce plus m                 -> row sums, for a matrix m (one result per row)
each abs (3 -4 5 -6)          -> 3 4 5 6
outer times (iota 3) (iota 3) -> the 3x3 multiplication table
```

`reduce` on a **matrix** folds along the last axis (each row collapses to
one number, producing a vector — one sum/max/etc. per row). `scan` and
`outer` currently accept vector arguments only.

`outer`'s two array arguments must each be a single **operand** (a bare
vector name/literal, or a fully parenthesized sub-expression) — write
`outer times (a + 1) (b * 2)` rather than an un-parenthesized compound
expression, so the parser can tell where the first array ends and the
second begins.

---

## 6. Variables and assignment

```
v <- iota 5        # assignment: nothing printed
total <- reduce plus v
total              # prints 15
```

Variable names are ordinary identifiers (letters, digits, underscore,
starting with a letter). The reserved words listed in §5 (and `reduce`,
`scan`, `each`, `outer`) can't be used as variable names.

---

## 7. Worked examples

### 7.1 Vectors, shape, and reduction
```
v <- iota 5              # 1 2 3 4 5
rho v                    # 5
reduce plus v            # 15
2 take v                 # 1 2
-2 take v                # 4 5
reverse v                # 5 4 3 2 1
```

### 7.2 Building and manipulating a matrix
```
m <- (2 3) rho (1 2 3 4 5 6)
m
#   1 2 3
#   4 5 6
rho m                    # 2 3
transpose m
#   1 4
#   2 5
#   3 6
reduce plus m            # 6 15   (row sums)
m matmul (transpose m)   # 3x3 "Gram matrix" of dot products between rows
```

### 7.3 Boolean masking and selection
```
v <- iota 10
(v mod 2) = 0            # 0 1 0 1 0 1 0 1 0 1   (which entries are even)
((v mod 2) = 0) compress v   # 2 4 6 8 10
```

### 7.4 A sieve-style prime finder (see `examples/primes.apl`)
```
n <- 30
cand     <- 1 drop (iota n)          # 2 3 4 ... n
divtable <- outer mod cand cand      # divtable[i,j] = cand[i] mod cand[j]
iszero   <- divtable = 0             # 1 where cand[j] divides cand[i]
counts   <- reduce plus iszero       # number of divisors of each candidate
isprime  <- counts = 1               # exactly one divisor (itself) -> prime
primes   <- isprime compress cand
primes                                # 2 3 5 7 11 13 17 19 23 29
```

---

## 8. Quad system variables and functions

Since APL's `⎕` (quad) glyph isn't ASCII either, system variables and
functions are spelled `[]NAME` in this dialect — `[]IO` instead of `⎕IO`,
and so on. A `[]NAME` token reads as a single value wherever a variable
could appear, and the settable ones can be assigned to with `<-` just like
an ordinary variable.

| Name    | Settable? | Meaning                                                          |
|---------|-----------|-------------------------------------------------------------------|
| `[]IO`  | yes (`0` or `1`) | **Index origin.** Controls where `iota` starts counting and how `index` subscripts are interpreted. Default `1`, matching traditional APL. |
| `[]PP`  | yes (non-negative integer) | **Print precision.** Number of decimal places shown for non-integer results. Default `6`. |
| `[]TS`  | no (read-only) | **Timestamp.** A 6-element vector: `year month day hour minute second`, read from the system clock at the moment it's evaluated. |

```
[]IO              # 1  (the default)
iota 5            # 1 2 3 4 5
v <- iota 5
v index 1         # 1  (first element, origin 1)

[]IO <- 0
iota 5            # 0 1 2 3 4
v index 0         # 1  (first element, now addressed as index 0)

[]PP <- 2
10 / 3            # 3.33
[]PP <- 6
10 / 3            # 3.333333

[]TS              # e.g. 2026 8 30 14 56 24
```

Only `iota` and `index` currently consult `[]IO`; every other primitive's
behaviour is origin-independent (shapes, counts, and boolean masks don't
shift meaning with the origin the way subscripts do).

### 8.1 The bare quad: `[]` (or `quad`) for input and output

`[]` on its own (no name) isn't a stored variable — it's real APL's idiom
for **explicit input and output**, spelled `quad` as a word if you'd
rather not use the brackets (both are exactly equivalent; `quad` is
reserved the same way `nabla`, `edit`, and `lamp` are).

**Output**: assigning to the bare quad evaluates the expression and
displays it immediately, exactly the way a bare (unassigned) statement
would. It's most useful inside a `nabla`-defined function, where you
might otherwise want to build a value and use it further without also
wanting it displayed:

```
apl> []<-'starting calculation'
starting calculation
apl> quad<-5+3
8
```

**Input**: reading the bare quad as a value — rather than assigning to
it — prompts for a line of input, evaluates whatever you type as a
single statement, and returns its result. This is genuine interactive
input, real APL's `⎕` used on the right of an assignment:

```
apl> v <- quad
      iota 5
apl> v
1 2 3 4 5
```

(`quad<-...` and `[]<-...` — assignment — always mean *output*. Reading
`quad` or `[]` as a plain value — anywhere an expression is expected,
including as a whole statement — always means *input*.)

> **Note if you're defining a function that uses `[]<-...`:** while
> inside the `nabla` line editor, a line is only treated as an editing
> directive (`[n]`, `[n.m]`, `[-n]`) when there's an actual number
> between the brackets. `[] <- expr` has nothing between them, so it's
> correctly read as an ordinary body line, not a directive.

---

## 9. System commands

A line whose first non-blank character is `)` is a **session command**,
handled directly rather than parsed as an APL expression. These work both
at the interactive `apl>` prompt and inside a script file.

Several of these treat the current directory as a small **workspace
library**: a "workspace" here is just an `.apl` file that `)SAVE` wrote
out, containing your variables and function definitions. You refer to it
by a plain **name**, without the `.apl` extension — matching real APL,
where workspace names never carry a file extension.

`)LOAD name` resolves `name` flexibly rather than assuming only one
form: it first tries `name.apl` (the normal case, since that's what
`)SAVE` produces), and only falls back to `name` exactly as given if
that doesn't exist (so a file that genuinely has no extension, or a
different one, still loads). `)SAVE` adds `.apl` when writing a new
workspace out (unless the name you gave already ends in `.apl`), and
the workspace id afterward is always the extension-free form, whichever
way it was found.

| Command         | Effect                                                               |
|-----------------|------------------------------------------------------------------------|
| `)VARS`         | list the names of currently-defined variables, sorted, space-separated (just names — evaluate a name as an expression to see its shape/value) |
| `)FNS`          | list user-defined function names, by header (e.g. `Z<-SQUARE X`) — see §10 for defining one with `nabla`/`edit` |
| `)PRIMS`        | list the built-in primitive functions, operators, and quad variables  |
| `)LIB`          | list saved workspaces (`.apl` files) in the current directory, marking the active one (see `)WSID`) |
| `)WSID` [name]  | with no argument, show the **active workspace id**; with a name, rename the in-memory workspace (does **not** save anything — use `)SAVE` for that) |
| `)CLEAR`        | wipe all variables and functions, reset `[]IO`/`[]PP` to their defaults, and reset the workspace id to `CLEAR WS` (a fresh workspace, matching real APL's exact default name) |
| `)SAVE` [name]  | save all current variables and functions as a workspace. With no name, saves under the active `)WSID`; either way, the workspace id becomes that name afterward. Does **not** persist `[]IO`/`[]PP`. |
| `)LOAD name`    | load a saved workspace into the **current** session — variables and functions persist afterward, the workspace id becomes `name`, and (like the REPL itself) a bad line prints a message and continues rather than aborting the load |
| `)ERASE name [name2 ...]` | delete one or more variables and/or functions by name from the current workspace, reporting each as erased or not found |
| `)HELP` or `)?` | show a short summary of the commands above                              |
| `)QUIT` `)EXIT` `)OFF` | end the session (or stop a script, if used inside a file)        |

A typical save/restore round trip:

```
apl> v <- iota 5
apl> m <- (2 3) rho (iota 6)
apl> )save mylib
saved 2 variable(s) and 0 function(s) to mylib.apl
apl> )wsid
mylib
apl> )clear
workspace cleared.
apl> )wsid
CLEAR WS
apl> )lib
workspaces available in this directory:
  mylib
apl> )load mylib
loaded mylib.apl
apl> v
1 2 3 4 5
apl> )wsid
mylib
apl> )erase m
erased: m
apl> )vars
v
```

> **Note on `)LIB`:** it shells out to the operating system's `ls` command
> to list `.apl` files, so it works as-is on Unix-like systems (Linux,
> macOS). On Windows Regina you'd substitute an equivalent `dir`-based
> command if you need this to work there too.

---

## 10. User-defined functions (`nabla` / `edit`)

Beyond the built-in primitives, you can define your **own** functions using
this dialect's version of the original APL function editor. In real APL
this is opened with the **del** glyph, `∇` ("nabla") — since that glyph
isn't ASCII either, this dialect spells it `nabla`, with `edit` as an exact
synonym for both opening and closing the editor.

This isn't a loose reinterpretation: the header syntax, the `[1]`-style
line prompting, and the fractional line-number insertion scheme below are
taken directly from IBM's original APL line-editor specification (as
documented for the ISO APL standard, which preserves APL\360's original
design), adapted only where the real editor used non-ASCII glyphs.

### 10.1 Defining a function

Type `nabla` (or `edit`) followed by a **header line**, at the `apl>`
prompt or as a line in a script:

```
apl> nabla Z<-SQUARE X
     Z<-SQUARE X
[1]  Z<-X*X
[2]  nabla
     SQUARE defined.
apl> SQUARE 5
25
apl> SQUARE iota 4
1 4 9 16
```

You typed `Z<-SQUARE X`, then the body line `Z<-X*X`, then bare `nabla` to
close and save the definition. Once defined, `SQUARE` behaves exactly like
a built-in primitive — it can be used monadically, dyadically, or (if
niladic) as a bare value, and it participates in the same right-to-left
evaluation as everything else.

**Header forms** (optionally followed by `;LOCAL1;LOCAL2` to declare local
variable names):

| Header                    | Arity    | Meaning                                    |
|---------------------------|----------|----------------------------------------------|
| `NAME`                    | niladic  | no arguments, no result                       |
| `Z<-NAME`                 | niladic  | no arguments, returns `Z`                     |
| `NAME X`                  | monadic  | one argument `X`, no result                   |
| `Z<-NAME X`               | monadic  | one argument `X`, returns `Z`                 |
| `X NAME Y`                | dyadic   | two arguments `X` (left) and `Y` (right), no result |
| `Z<-X NAME Y`             | dyadic   | two arguments, returns `Z`                    |
| `Z<-AVG V;S;N`            | monadic  | as above, plus locals `S` and `N`             |

A function's **result** is whatever its declared result variable (`Z` in
the examples above) holds when the last body line finishes executing. If
a result variable was declared but the function never assigns it, calling
the function is a runtime error. If no result variable was declared, the
function has no value — calling it as a whole statement is fine (useful
for its effect on global variables), but using its call inside a larger
expression isn't supported.

The function name can't collide with a primitive, a currently-defined
variable, or the words `nabla`/`edit` themselves. Its parameters, result
variable, and locals also can't collide with the function's **own**
name, or with any other existing function or primitive — variable and
function names share one namespace here, exactly as in real APL, so
`nabla Z<-SUM SUM` (result and function both named `SUM`) or a parameter
named after another function you've already defined are both rejected
with a clear message rather than being allowed to silently misbehave.

### 10.2 Parameters, locals, and scoping

Parameters, the result variable, and any declared locals are **localized**
exactly the way real APL localizes them: each one's prior global value (if
it had one) is saved when the function is called and restored when the
function returns, using a depth-stamped stack so nested and recursive
calls each get their own bindings. This means a function's parameters and
locals can safely reuse names you're already using for other variables at
the top level — the global value is unaffected once the call returns.

```
apl> v <- 100
apl> nabla Z<-AVG V;S;N
[1]  S<-reduce plus V
[2]  N<-rho V
[3]  Z<-S/N
[4]  nabla
     AVG defined.
apl> AVG (iota 4)
2.5
apl> v
100
```

Even though the function's parameter is also named `V`, the outer `v`
(REXX/this dialect's variable names are case-sensitive, so `v` and `V` are
actually different names here, but the same localization applies if you
do reuse a name) is untouched.

### 10.3 Displaying a function without editing it

To just **look at** a function without risking any change, type `nabla`
(or `edit`), the function's name, and `nabla` again — all on one line.
This is the original editor's own documented shorthand for "open and
immediately close with nothing changed":

```
apl> nabla SQUARE nabla
     Z<-SQUARE X
[1]  Z<-X*X
apl>
```

Nothing is prompted for afterward and nothing is saved — you're back at
the normal prompt immediately. (If you instead want to *edit* the
function, see below and just don't add the trailing `nabla`.)

### 10.4 Editing an existing function

Type `nabla` (or `edit`) followed by just the function's **name** (no
header) to reopen it. This first displays the function as it currently
stands, then prompts for further input starting one line past the current
end — exactly matching the original editor's behavior:

```
apl> nabla SQUARE
     Z<-SQUARE X
[1]  Z<-X*X
[2]  nabla
```

From here you can type new lines to append, or use an **editing
directive** as the first thing on a line:

| Directive         | Effect                                                        |
|-------------------|------------------------------------------------------------------|
| `[n] text`        | replace line `n` with `text` — or insert a new line `n` if there is none |
| `[n]`             | redisplay line `n`'s current text, unchanged                     |
| `[n.m] text`      | insert a new line **between** existing lines, using a fractional line number (up to 3 decimal places) — the original editor's own mechanism for insertion |
| `[-n]`            | delete line `n` (this dialect's ASCII stand-in for the original editor's `[Δn]`, since `Δ` isn't ASCII either) |
| `[]`              | display the whole function (header + all lines) so far            |
| `nabla` or `edit` | close the editor and save                                         |

Fractional line numbers behave the same way they did originally: after
positioning at, say, `[2.3]`, the *next* auto-numbered prompt advances by
the same decimal step (`[2.4]`), not by a whole integer — matching the
documented rule that the increment is "a unit of the display form of the
last decimal digit." When you close the editor, all lines are renumbered
to consecutive integers for permanent storage.

```
apl> nabla SQUARE
     Z<-SQUARE X
[1]  Z<-X*X
[2]  [1.5] Z<-Z+1
[2]  nabla
     SQUARE defined.
apl> SQUARE 5
26
```

### 10.5 `)FNS`, `)SAVE`, and `)LOAD`

`)FNS` lists your defined functions by header (see §9 for the full system-command reference). `)SAVE` writes
functions out as `nabla ... nabla` blocks alongside variable assignments,
and `)LOAD` (and `)CLEAR`) understand those blocks too, so a full
save/clear/load round trip restores both your variables and your
functions:

```
apl> )save mylib
saved 1 variable(s) and 1 function(s) to mylib.apl
apl> )clear
workspace cleared.
apl> )load mylib
loaded mylib.apl
apl> SQUARE 6
36
```

### 10.6 Branching: `->`, labels, and conditionals

Real APL's function editor supports **branch** (`→`, to a line number or
a label) as its mechanism for conditionals and loops within a function.
This dialect implements the same thing, spelled `->` since `→` isn't
ASCII either.

**Unconditional branch**, by line number or by label:

```
-> 5        # jump to line 5
-> LOOP     # jump to the line labelled LOOP:
->          # bare arrow: exit the function immediately (like a return)
```

A **label** is written as `NAME:` tight against the start of a line (no
space before the colon) — either alone on its own line, as a pure branch
target, or immediately before a statement on the same line:

```
LOOP: Z<-Z+1
```

Labels are resolved the same way parameters and locals are: each one is
bound, for the duration of the call, to its own line number as an
ordinary value — which is why `-> LOOP` works through the same expression
evaluator as everything else, with no special-case machinery needed. Like
parameters and locals, labels are **localized**: they don't leak out to
the rest of your session, and recursive/nested calls each get their own.

**Conditional branch** uses the classic APL idiom, built from `compress`
rather than a dedicated `if`:

```
-> (N <= 1) compress DONE
```

`compress` here is standing in for APL's `/` glyph (unavailable to us
since `/` is already division). If the condition is `1`, `compress`
produces a one-element vector holding the target, and the branch is
taken; if `0`, it produces an empty vector, and branching to an empty
vector means "don't branch" — execution just falls through to the next
line. This is exactly how real APL builds `if`-like behavior out of
array primitives rather than a separate control-flow keyword.

Putting both together, an actual recursive-style function — the very
thing §10.6 in earlier drafts of this document said wasn't possible —
now works:

```
apl> nabla Z<-FACT N
[1]  Z<-1
[2]  LOOP:
[3]  -> (N<=1) compress DONE
[4]  Z<-Z*N
[5]  N<-N - 1
[6]  -> LOOP
[7]  DONE:
[8]  nabla
apl> FACT 5
120
```

(Note the spaces in `N - 1`: per §3's minus-sign rule, `N-1` glued
together would be misread as two juxtaposed values rather than
subtraction.)

`->` only has meaning inside a function body; using it as a plain
statement (at the `apl>` prompt, in a script, or via `)LOAD`) prints a
message and is otherwise ignored, since there are no numbered lines to
jump between outside a function.

### 10.7 `[]EA` — Execute Alternate (try/catch)

`[]EA` is this dialect's version of real APL's `⎕EA`, and is the closest
thing here to try/catch. Both operands are **text values** holding a
single statement's source; the primary is evaluated first, and if it
raises no error, `[]EA` returns its result. If it *does* error, that
error is caught silently (never displayed) and the alternate is
evaluated and returned instead. If the alternate *also* errors, that
second error is not caught — it displays and propagates normally, the
same as any other error.

```
apl> '10 / 2' []EA '999'
5
apl> '10 / 0' []EA '999'
999
apl> '10 / 0' []EA 'undefinedvar'
apl: syntax error -- undefined variable: undefinedvar
```

Since the operands execute in the **current scope**, `[]EA` is
particularly useful inside a function for guarding a risky operation
without aborting the whole call:

```
apl> nabla Z<-SAFE X
[1]  Z<-'X index 99' []EA '-1'
[2]  nabla
apl> SAFE (iota 5)
-1
```

---

## 11. Known limitations of this first version

- **Rank is capped at 2** (scalars, vectors, matrices). `take`, `drop`,
  `reverse`, `rotate`, `compress`, `index`, `matmul`, `reduce`, and `scan`
  all raise a runtime error if given a higher-rank array. (`rho`/reshape can
  still *build* a higher-rank shape; it just can't be operated on further —
  it will print as a flat "shape … : …" listing.)
- **No `if`/loop keywords**, only `->` branching. Top-level statements
  (and script/`)LOAD` files) are still straight-line — `->` only works
  inside a `nabla`-defined function body, where it can build loops and
  conditionals the classic APL way (see §10.6). There's no structured
  `if`/`while`/`for`; every control-flow pattern is expressed via `->`
  and labels, matching how the original scan-driven microcode model (and
  early APL generally) worked. The `examples/primes.apl` sieve shows how
  far array-oriented "loopless" idioms can carry you even without that.
- **`outer` and `scan`** accept vector operands only (not matrices).
- Numeric precision follows REXX's `NUMERIC DIGITS 20` setting; very long
  division chains may show more or fewer decimal digits than a dedicated
  APL system would.
- Index origin defaults to **1** but is now settable via `[]IO` (see §8).
- In **script mode**, a syntax or runtime error still aborts the whole
  script with exit code 1 (so a broken script fails loudly, as before). In
  **REPL mode**, the same error just prints a message and returns you to
  the prompt — the rest of your session, and any variables or functions
  you've already defined, are unaffected.
- Editing the **header line** of an existing function (line `[0]` in the
  original editor's numbering) isn't supported — redefine the function
  from scratch (`nabla NEWHEADER`) if you need to change its arity,
  parameter names, or result variable.

---

## 12. File listing

| File                        | Purpose                                             |
|-----------------------------|------------------------------------------------------|
| `apl.rex`                   | the interpreter itself                                |
| `regina_apl_doc.md`         | this document                                         |
| `examples/matrix_demo.apl`  | reshape, transpose, matmul, take, reverse, rotate     |
| `examples/primes.apl`       | loopless sieve-of-Eratosthenes-style idiom            |
| `examples/functions_demo.apl` | defining and calling functions with `nabla`/`edit`  |
