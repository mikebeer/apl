module.exports = ({ chapter, h2, h3, p, ul, fig, run, snip, show, table, fs, path, ROOT }) => {
  // ======================================================== Preface
  chapter('Preface', { plain: true, first: true });
  p('This book describes APL, an interpreter for the APL language written in portable ANSI C. APL is an array language: a single statement can add two tables, sort a list, or search a text, and the work is done on whole arrays at a time. The language was designed by Kenneth Iverson and is best known today through IBM’s APL2 and through Dyalog APL.');
  p('The interpreter follows APL2 for the core of the language, including nested arrays, and adds a few extensions from Dyalog APL: lambda functions, structured control words, the rank operator, and some composition operators. It differs from both in one visible way. Instead of the special APL characters it uses English words: `iota` for ⍳, `rho` for ⍴, `reduce plus` for +/. A program is therefore plain text that can be typed on any keyboard and carried through any code page, which matters because the interpreter is meant to run, in due course, on the VM/370 operating system.');
  p('This book is for programmers who want to run APL programs or to learn the language, and for people who want to know how the interpreter is built. You do not need to know APL. You do need to know how to build a C program and run it from a command line.');
  h2('How this book is organized');
  ul([
    '**Chapter 1** says what the interpreter is and what it is not.',
    '**Chapter 2** shows how to build it and run a program.',
    '**Chapter 3** describes the language: words, evaluation, functions, control words, lambdas and operators.',
    '**Chapter 4** describes arrays, indexing and nested arrays.',
    '**Chapter 5** describes console input and output, the system variables and functions, the system commands, workspaces, and shared variables with auxiliary processors.',
    '**Chapter 6** is a reference for every primitive function and operator.',
    '**Chapter 7** presents complete example programs together with what they print.',
    '**Chapter 8** explains how the interpreter works and how to extend it, including how to write an auxiliary processor.',
    '**Chapter 9** lists the messages. **Chapter 10** lists the restrictions and known problems.',
    '**Appendix A** lists the system commands, **Appendix B** the files in the repository, and **Appendix C** the differences from APL2 and Dyalog APL.'
  ]);
  h2('Conventions');
  p('Names of functions, variables, commands and files are shown in `this type`. A figure that shows a program and its output separates the two with a line `# prints`; the output was produced by the interpreter when this book was built. Where a figure shows a session at a terminal, the characters you type follow the prompt `apl>` or a `$`.');
  p('APL glyphs appear in the tables of Chapter 6 so that readers who know APL can find their way around. The interpreter itself never reads a glyph.');

  // ======================================================== 1
  chapter('Introduction');
  p('APL runs programs written in the APL language. The language is applicative and array-oriented: every value is an array with a shape, functions take arrays and return arrays, and a statement is read from right to left with no operator precedence. The interpreter is one C program of some 6,000 lines, written in strict C89 so that it compiles on small systems.');
  p('The interpreter provides:');
  ul([
    '**Arrays.** Numeric, character and nested arrays of up to eight dimensions. A nested array holds arrays as its items.',
    '**About 90 primitive functions** for arithmetic, structure, searching, sorting, set operations, formatting and base conversion, and **thirteen operators**: reduce, scan, each, outer product, inner product, swap, repeat, until, rank, over and atop, plus amend and keyed.',
    '**User-defined functions** written in a line editor with labels and branches, and **lambdas** written in braces with `alpha` and `omega`. Operators of your own are defined with `dop`.',
    '**Control words** in the Dyalog style: `:If`, `:While`, `:For`, `:Repeat`, `:Select` and their relatives.',
    '**System variables and functions** such as `[]IO`, `[]CT`, `[]EA` (try/catch) and `[]SIGNAL`, and **workspaces** that can be saved, loaded and copied.',
    '**Shared variables and auxiliary processors.** The hooks are present, with three demonstration processors; Chapter 8 shows how to add one.'
  ]);
  p('The interpreter is not a full APL2 or Dyalog. Chapter 10 and Appendix C list what is missing and what differs.');
  h2('How the pieces fit together');
  p('The interpreter is built once from the C sources. After that, any number of programs can be run. The figure below lists the steps.');
  table('From source to output',
    ['Step', 'Program', 'Input', 'Output'],
    [['1', 'make', 'src/*.c', 'apl_all.c (one source file) and apl'],
     ['2', './apl prog.apl', 'Your program, prog.apl', 'The output of the program'],
     ['3', './apl', 'Lines typed at the terminal', 'The interactive session']],
    [800, 2600, 3200, 3038], { mono: [1] });
  p('The source of the interpreter is kept in six parts, which `tools/amalg.sh` joins into the single file `apl_all.c`. That file is the one to ship to a system that wants one source file.');

  // ======================================================== 2
  chapter('Installing and Running APL');
  h2('What you need');
  p('A C compiler that accepts ANSI C (C89), the C library with its math functions, and `make`. The tests also use a POSIX shell. Nothing else is required: the program makes no system calls apart from standard input and output, the clock, and an optional directory listing for the `)LIB` command.');
  h2('Building the interpreter');
  p('Run `make` in the top directory. The figure below shows what it does. The compiler options ask for strict C89 and for all warnings; the build is expected to be silent.');
  show('Building the interpreter', '$ make\nsh tools/amalg.sh > apl_all.c\ncc -O2 -std=c89 -pedantic -Wall -Wextra -o apl apl_all.c -lm\n\n$ make test\n...\npassed 20, failed 0');
  p('`make test` runs every program in `examples` and in `tests/scen` and compares what it prints with the expected output stored beside it. `make clean` removes the binary and the generated single file.');
  h2('Running a program');
  p('Give the name of a source file as the only argument. The figure below is the smallest program and what it prints. A statement that is an expression prints its value, so there is no print command.');
  run('examples/hello.apl', 'examples/hello.apl');
  p('The interpreter reads the file one line at a time. When a statement fails, it prints a message, stops, and ends with a return code of 1. Section ‘Errors’ below describes the message.');
  h2('Starting a session');
  p('Without an argument the interpreter starts a session. It prints a banner and the prompt `apl> `, reads a line, evaluates it, and prints the result. An error is reported and the session goes on. The figure below is a recorded session; the lines after the prompts were typed by the user. `)quit` ends the session.');
  show('A session', fs.readFileSync(path.join(ROOT, 'docs', 'repl_session.txt'), 'utf8'));
  h2('Programs that read the keyboard');
  p('A bare `[]` reads a line from standard input and evaluates it as an expression, so a quoted string comes back as text and a number as a number. When standard input is not a terminal, as in the figure below, the lines are taken from it in order.');
  run('Feeding answers to a program (examples/ask.apl)', 'examples/ask.apl', "'Mike'\n21\n");
  p('The first two lines of output are the prompts that the program prints itself; the answers were supplied by the pipe and are not echoed.');
  h2('Errors');
  p('A syntax error is a statement that cannot be parsed, such as a missing operand or an unknown name. A runtime error is raised while the statement runs, such as a `DOMAIN ERROR`. Both are reported on one line:');
  show('Error messages', 'apl: syntax error -- undefined variable: x\napl: runtime error -- DOMAIN ERROR: divide by zero');
  p('A script stops at the first error and the interpreter ends with a return code of 1. In a session the error is reported, any functions that were running are abandoned, and the prompt returns. The statement `\'expression\' []EA \'alternative\'` evaluates the alternative if the expression fails (Chapter 3). Chapter 9 lists the messages.');

  // ======================================================== 3
  chapter('The APL Language');
  h2('Words instead of glyphs');
  p('Every APL glyph has a name in lower case. The name is used exactly where the glyph would be: `iota 5` is ⍳ 5 and `3 plus 4` is 3 + 4. The words are reserved; a variable cannot have one of them as its name. The full table is in Chapter 6. The plain characters `+ - * / = < > <= >= <>` are also accepted as dyadic functions, with the meanings plus, minus, times, divide, equal, less, greater, not greater, not less and not equal. Note that `*` is multiplication and `/` is division, as in most programming languages, and not power and compress as in APL.');
  p('A name starts with a letter and continues with letters, digits and underscores. Names are case sensitive. A comment starts with `#` or with the word `lamp` and runs to the end of the line.');
  h2('Numbers and text');
  p('A number is written with digits, a decimal point and an exponent: `42`, `3.14`, `1e6`. A negative number is written with the function `neg`, as in `neg 5`, or with a leading minus sign, as in `- 5`. Numbers are stored as double-precision floating-point values. The system variable `[]PP` sets how many places after the decimal point are shown (default 6), and trailing zeros are not printed.');
  p('Text is written between single quotes. Two quotes inside a text stand for one: `\'it\'\'s\'` is the four characters `it\'s`. A text of one character is a scalar; any other length is a vector. There is no separate string type: text is an array of characters, and every array function works on it.');
  snip('Numbers and text', "'it''s'\nrho 'it''s'\n1 divide 3\n123456789 times 10\nneg 1.5 2\n1e10 times 1e10\n[]PP <- 2\n1 divide 3");
  p('A **complex number** is written with a J between its parts: `3J4`, `1.5J-2`. A negative real part is written with `-` or `neg` as usual. Chapter 6 describes how the functions treat them.');
  h2('Evaluation, right to left');
  p('A statement is evaluated from right to left. Every function takes everything to its right as its right argument. `2 times 3 plus 4` is `2 times (3 plus 4)`, which is 14. There is no precedence among functions; use parentheses to change the order.');
  snip('Right to left', '2 times 3 plus 4\n(2 times 3) plus 4\n10 minus 3 minus 2\n(10 minus 3) minus 2');
  p('A function called with a value to its left is **dyadic**; one with no left value is **monadic**. Most words have both forms with related meanings: `neg 5` is minus five, `7 minus 5` is two.');
  h2('Variables and assignment');
  p('`name <- expression` assigns. An assignment prints nothing. A name that has no value gives `undefined variable`. Variables live in the workspace until `)CLEAR`, `)ERASE` or `[]EX` removes them. Elements of an array can be assigned by index, `m[2;3] <- 0`; Chapter 4 describes this.');
  p('Part of an array can be assigned by **selective assignment**: a selection of the variable, in parentheses, on the left of the arrow. `(2 take v) <- 0` sets the first two items to 0, `((v > 3) compress v) <- 0` clears the items above 3, and `(reverse v) <- w` assigns w backwards. The selection may use `take`, `drop`, `reverse`, `rotate`, `transpose`, `ravel`, `compress`, `expand`, `rho`, `pick`, `first`, `disclose` and `index`, applied to the variable and to nothing else; the variable is the rightmost name in the parentheses. The right side must be a scalar or have the same shape as the selection.');
  snip('Selective assignment', "v <- 1 2 3 4 5 6\n(2 take v) <- 0\nv\n((v > 3) compress v) <- 9\nv\n(reverse v) <- iota 6\nv\nm <- 3 3 rho iota 9\n(transpose m) <- 3 3 rho 0\nm");
  h2('Defining functions');
  p('A function is defined in the line editor. The word `nabla` on a line of its own, followed by a header, starts the editor; the next `nabla` ends it. The header names the function, its arguments and its result:');
  table('Function headers',
    ['Header', 'Kind'],
    [['nabla Z<-A NAME B', 'dyadic function with a result'],
     ['nabla Z<-NAME B', 'monadic function with a result'],
     ['nabla NAME B', 'monadic function with no result'],
     ['nabla Z<-NAME', 'niladic function with a result'],
     ['nabla Z<-A NAME B;P;Q', 'the same, with local variables P and Q']],
    [4800, 4838], { mono: [0] });
  p('A function that is called with a missing argument, or that returns no value where one is used, is a syntax error. A name used in the body that is not declared local is global. The arguments, the result and the labels are always local. Functions may call themselves and each other; the depth is limited (Chapter 10).');
  p('Inside the editor each line is a statement. A label is a name followed by a colon at the start of a line: `LOOP: ...`. `-> n` branches to line n, `-> LABEL` to a label, and `->` alone leaves the function. A conditional branch uses `compress`: `-> (X<=0) compress DONE` branches when the condition is 1 and falls through when it is 0, because compressing a label by 0 gives an empty vector, and branching to an empty vector does nothing.');
  p('The figure below is a small program with two functions, a label and a conditional branch. `REPORT` returns a text, and the last statement uses `[]EA` to catch an error. Defining a function prints its header and `NAME defined.`; the figures in this book leave these two lines out.');
  run('Functions and branches (examples/bank.apl)', 'examples/bank.apl');
  h2('Control words');
  p('Inside a function the lines of a block can also be structured with control words, one per line, which start with a colon. They work in the Dyalog way. A condition must be a numeric scalar; any value other than 0 is true.');
  table('Control words',
    ['Control word', 'Meaning'],
    [[':If c  :ElseIf c  :Else  :EndIf', 'Run the first block whose condition is true.'],
     [':While c  :EndWhile', 'Repeat the block while the condition is true.'],
     [':Repeat  :Until c', 'Run the block, then repeat until the condition is true.'],
     [':For v :In x  :EndFor', 'Run the block once for every item of x, with v set to the item.'],
     [':Select x  :Case y  :CaseList ys  :Else  :EndSelect', 'Run the block of the first case: :Case when x matches y, :CaseList when x is a member of ys.'],
     [':Leave', 'Leave the innermost loop.'],
     [':Continue', 'Go to the next repetition of the innermost loop.'],
     [':Return', 'Leave the function.']],
    [4100, 5538], { mono: [0] });
  p('Control words are not statements of the session; they are valid only in a function. A `:For` variable is an ordinary variable of the function and is not made local unless it is named in the header. Words are matched without regard to case, so `:if` and `:If` are the same.');
  run('Control words (examples/control.apl)', 'examples/control.apl');
  h2('Lambdas');
  p('A function can be written in braces where it is used or assigned to a name. Inside, `omega` is the right argument and `alpha` the left; a lambda that uses `alpha` is dyadic. A lambda is a list of statements separated by semicolons; a statement of the form `cond : value` is a guard that returns the value if the condition is true. `self` calls the lambda recursively. A name assigned inside a lambda is local to it.');
  run('Lambdas (examples/lambda.apl)', 'examples/lambda.apl');
  p('`name <- {...}` makes a named function, which `)FNS` lists and `[]CR` can show. A lambda may be used as the operand of an operator as in `each {omega plus 1} v`.');
  h2('Operators');
  p('An operator takes functions as operands and makes a new function. This interpreter writes an operator in front of its operands, so what APL writes `+/v` is written `reduce plus v`. The figure below lists them; Chapter 6 gives the details.');
  table('Operators',
    ['Operator', 'Form', 'Meaning'],
    [['reduce', 'reduce f x', 'Insert f between the items of x along the last axis; `reduce f[1] x` along the first; `n reduce f x` takes n items at a time.'],
     ['scan', 'scan f x', 'The running reduction: the reduction of each prefix.'],
     ['each', 'each f x; x each f y', 'Apply f to every item of x, or to the matching items of x and y.'],
     ['outer', 'outer f x y', 'Apply f to every pair of an item of x and an item of y; `x outer f y` is also allowed.'],
     ['inner', 'x inner f g y', 'The generalized matrix product; `inner plus times` is the ordinary product.'],
     ['swap', 'swap f y; x swap f y', 'Apply f with the arguments exchanged; with one argument, use it on both sides.'],
     ['repeat', 'repeat f n y', 'Apply f n times.'],
     ['until', 'until f g y', 'Apply f until the test g, applied to the new and old values, is true.'],
     ['rank', 'rank f k y', 'Apply f to each cell of rank k of y.'],
     ['over', 'x over f g y', 'Apply g to each argument, then f: (g x) f (g y). Monadic: f g y.'],
     ['atop', 'x atop f g y', 'Apply g to the arguments, then f to the result: f (x g y).'],
     ['amend', 'v amend i y; amend i f y', 'Replace the items of y at indexes i by v, or by f applied to those items.'],
     ['keyed', 'x keyed f y', 'For each distinct key in x, call f with the key and the matching items of y (indexes when there is no y).']],
    [1300, 2900, 5438], { mono: [0, 1] });
  p('The operand of an operator is a function: a word such as `plus`, a user function, or a lambda. An operator takes everything to its right as its argument, like a monadic function. Parentheses around a function are not allowed.');
  run('Operators (examples/operators.apl)', 'examples/operators.apl');
  h2('Operators of your own');
  p('An operator is defined as a `dop`: `name <- dop { ... }`. Inside the braces, `alphaalpha` stands for the first operand function and `omegaomega` for the second; the operator has two operands if the body uses `omegaomega`, otherwise one. `alpha` and `omega` are the arguments of the derived function as in an ordinary lambda. The operands are put in the body as text before it is run, so any function or lambda may be used.');
  run('A dop (examples/userop.apl)', 'examples/userop.apl');
  h2('Errors and traps');
  p('`\'expr\' []EA \'alt\'` evaluates the text expr; if that fails, it evaluates alt instead and returns its value. `[]SIGNAL n` raises the error with number n (for instance 11 for a domain error), and `[]SIGNAL \'text\'` raises an error with your own text. After an error, `[]DM` holds the message and `[]EN` the number.');
  snip('Catching errors', "'1 divide 2' []EA '''failed'''\n'1 divide 0' []EA '''failed'''\n'[]SIGNAL ''out of cheese''' []EA '''caught'''\n[]EN\nreduce plus 0 rho 0");
};
