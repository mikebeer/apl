module.exports = ({ chapter, h2, h3, p, ul, fig, run, snip, show, table, fs, path, ROOT }) => {
  // ======================================================== 7
  chapter('Examples');
  p('The programs in this chapter are in the directory `examples` of the repository, and each figure shows what the interpreter printed for it when this book was built. `make test` runs the ones that need no input and compares the output with the stored copy.');
  h2('Prime numbers');
  p('A prime has no divisors but itself and 1. The program makes a table of the remainders of every number from 2 to 30 divided by every candidate divisor, counts the zeros in each column, and keeps the numbers that have exactly one divisor, which is the number itself. The table is built by the `outer` operator with `mod`, and the counting by `reduce plus[1]`, which adds down the columns. There is no loop.');
  run('examples/primes.apl', 'examples/primes.apl');
  h2('Statistics');
  p('Three small functions that call each other: `STDDEV` uses `MEAN`. `MEDIAN` sorts with `gradeup` and takes the middle one or two items. The functions are defined with `nabla` and used like the primitives.');
  run('examples/stats.apl', 'examples/stats.apl');
  h2('The game of life');
  p('Conway’s Game of Life on a six by six grid whose edges wrap around. `NEIGHBOURS` counts the live neighbours of every cell by adding the grid to copies of itself rotated by one place in each direction, with `rotate` along the first axis (`rotate[1]`) and the last. `STEP` applies the rules. `SHOW` prints a grid by using the 0s and 1s as an index into the two characters blank and `#`. The picture is a glider; it moves one cell diagonally in four steps.');
  run('examples/life.apl', 'examples/life.apl');
  h2('A guessing game');
  p('The program picks a number with `roll` after setting `[]RL`, so that every run picks the same number. It then reads guesses with `[]` and answers `lower` or `higher` until the guess is right. The function uses labels and conditional branches; it could be written with `:While` as well. The figure shows a run where the guesses came from a pipe.');
  run('A run of examples/guess.apl', 'examples/guess.apl', '50\n25\n10\n5\n3\n');
  h2('The example files');
  table('The example programs',
    ['File', 'What it shows'],
    [['hello.apl', 'The smallest program'],
     ['ask.apl', 'Reading from the keyboard'],
     ['arrays.apl', 'Shape, indexing, axes'],
     ['nested.apl', 'Nested arrays'],
     ['scalar.apl', 'Scalar functions'],
     ['structure.apl', 'Sorting, searching, sets, partitions'],
     ['strings.apl', 'Text'],
     ['operators.apl', 'The operators'],
     ['lambda.apl', 'Lambdas, recursion and guards'],
     ['control.apl', 'Control words'],
     ['userop.apl', 'Operators defined with dop'],
     ['procs.apl', 'Shared variables and processors'],
     ['bank.apl', 'Functions, locals, branches, error trapping'],
     ['stats.apl', 'Statistics'],
     ['life.apl', 'The game of life'],
     ['primes.apl', 'The prime sieve'],
     ['guess.apl', 'A guessing game'],
     ['basic_smoke_test.apl, edge_cases_test.apl, functions_demo.apl, matrix_demo.apl, text_demo.apl', 'Test programs inherited from the REXX prototype']],
    [4200, 5438], { mono: [0] });

  // ======================================================== 8
  chapter('How the Interpreter Works');
  h2('The parts');
  p('The interpreter is written in C89 as one translation unit. `src/apl.c` includes the parts in the order of the table below; `tools/amalg.sh` expands the includes into the single file `apl_all.c`, which `make` compiles. Every function and variable is `static`; only `main` is visible to the linker.');
  table('The source files',
    ['File', 'Contents'],
    [['p0_base.c', 'Headers, limits, character classes, memory allocation, the error mechanism'],
     ['p1_val.c', 'Arrays: types, reference counts, constructors, simplification, display'],
     ['p2_lex.c', 'The tokenizer, and the registry that frees temporaries after an error'],
     ['p3_prim.c', 'The scalar and structural primitives, and the table of words'],
     ['p3b_ap.c', 'Auxiliary processors and shared variables: the hooks and the demonstration processors'],
     ['p3c_fmt.c', '`[]FMT`: the format specification and the formatter'],
     ['p3d_proc.c', 'The processors with real code: FILES, SCREEN, TCP and COMPFILE, and the platform-dependent parts (sockets, terminal)'],
     ['p4_eval.c', 'Variables, user functions, control words, the parser and evaluator, the operators, selective assignment'],
     ['p4b_oo.c', 'Namespaces, classes and instances: scopes, access rules, method dispatch, release of objects'],
     ['p5_sys.c', 'The function editor, system functions and commands, workspaces, the session loop and `main`'],
     ['p5b_oosys.c', 'The block parsers for `:Class`, `:Namespace` and `:Interface`, the object system functions, save and load of objects']],
    [2000, 7638], { mono: [0] });
  h2('Values');
  p('A value is a reference-counted array with a type, a rank, a shape and its items. The type is one of four: `T_NUM` holds doubles, `T_CPX` holds complex numbers as two arrays of doubles (real and imaginary parts), `T_CHR` holds characters, and `T_NEST` holds pointers to other values. After a function builds a nested array, `a_simplify` turns it into a numeric or character array if every item is a simple scalar, so there is one representation for each value.');
  h2('Evaluation');
  p('The evaluator is a recursive-descent parser that evaluates as it parses. `parse_expr` reads one operand (a strand of items), looks at the token after it, and decides whether it is a dyadic function, an operator or the end; if it is a function it evaluates everything to the right first, which is what gives right-to-left evaluation. Operators are looked up the same way, before their operands. A function written in braces is turned into a statement list once and cached by its text. Control words are recognized in a user function when it is first called: the matching `:End` of each is found by a scan and the jumps are stored with the function, so a loop costs no more than a branch.');
  h2('Errors');
  p('An error is raised with `rt_error` or `syn_error`, which fill in the message and call `longjmp` to the nearest handler: the session loop, or the `[]EA` that is running. Temporary buffers and token arrays are entered in a registry, and the handler releases whatever the failed statement still held. Local variables of the functions that were interrupted are restored by unwinding the call frames.');
  h2('Limits and portability');
  p('The program is written so that it can be compiled for VM/370, where the available C compiler is GCC 3.8 (GCCLIB). That decides several choices: there are no 64-bit integers, so array sizes and indexes are 32-bit `int` and numbers are `double`; floating point on that machine is hexadecimal, so the largest number is about 7.2e75 and the interpreter reports a DOMAIN ERROR beyond 7.0e75 on every machine, so that results agree; the character set is EBCDIC, so no code compares character values with ranges; external names are limited to eight characters, which is why nothing but `main` is external; and the source must fit in 80-column records. Chapter 10 lists what is still to be done for that port.');
  h2('Adding a primitive');
  ul([
    'Add an identifier to the enumeration at the top of `p3_prim.c` and a line to the table `PRIMS`: the word, the identifier, and whether it has a monadic and a dyadic form.',
    'Write the function, taking and returning `Arr *`. Arguments belong to the caller; the result is a new reference. Raise errors with `rt_error`.',
    'Add a `case` to `prim_mon0` or `prim_dya0`. A scalar function is added to `sdya0` or `smon0` instead and gets pervasion for free.',
    'Add a test program under `tests/scen`, run it, check the output by eye, and keep it as the expected output.'
  ]);
  h2('Writing an auxiliary processor');
  p('A processor is a set of up to four C functions, called hooks, that are registered in `ap_init` in `p3b_ap.c`. The variable layer calls them: `offer` when `[]SVO` offers a variable, `get` when the variable is referenced, `set` after it is assigned, and `retract` when the offer ends. A hook that is not needed is a null pointer, and then the variable behaves like an ordinary one. `get` returns a new value for the reference, or null to supply the value that was last stored; `set` returns a new value to store, or null to keep the assigned value. Hooks report errors with `rt_error`. The figure below is the whole of the UPPER processor.');
  const src = fs.readFileSync(path.join(ROOT, 'src/p3b_ap.c'), 'utf8');
  const m1 = src.match(/\/\* AP 2: UPPER[\s\S]*?\n}\n/);
  const m2 = src.match(/static void ap_init\(void\)[\s\S]*?\n}\n/);
  const reg = m2[0].split('\n').filter(l => /UPPER|ap_init|^{|^}|ap_n = 0/.test(l)).join('\n');
  show('A processor: AP 2 (UPPER)', m1[0].replace(/\n$/, '') + '\n\n' + reg.replace(/\n$/, ''));
  p('The number 126 is registered with `installed` set to 0, which makes `[]SVO` refuse it. To install a processor, write its hooks, register it with `installed = 1` and rebuild. The processors of Chapter 12 are registered the same way in `ap_init_extra` in `p3d_proc.c`; their hooks use the request protocol described there, which needs only a `set` hook that returns the result. A processor in C can hold any state it needs; it can keep a file open, for instance, and make each assignment to its variable a write and each reference a read. The hooks run in the middle of the evaluation of a statement, so they should be quick and they must leave the arrays they were given alone.');

  // ======================================================== 9
  chapter('Messages');
  p('Every message is printed on one line, preceded by `apl: runtime error --` or `apl: syntax error --`. The text after the dashes is the message. The families below are shared with APL2: those that start with a name in capitals also set `[]EN`.');
  table('Run time messages',
    ['Message', 'Meaning'],
    [['DOMAIN ERROR: text', 'The argument has the wrong kind of value or an impossible value: text for a number, division by zero, a number beyond 7.0e75, a count that is not a whole number, a singular matrix.'],
     ['RANK ERROR: text', 'The argument has the wrong number of axes, for instance a matrix where a vector is needed.'],
     ['LENGTH ERROR: text', 'The shapes of the arguments do not agree.'],
     ['AXIS ERROR: text', 'The axis in brackets is not a number or does not exist.'],
     ['INDEX ERROR: subscript out of range', 'An index is below the origin or above the length.'],
     ['DEPTH ERROR: text', 'Functions or loops are nested more deeply than the interpreter allows.'],
     ['WS FULL', 'The array would have more items than `[]WA`.'],
     ['[]: end of input', 'A bare `[]` read and there was no more input.'],
     ['[]IO must be 0 or 1  (and similar)', 'A system variable was assigned an impossible value.'],
     ['until: no convergence after 1000000 steps', 'The test of `until` never became true.'],
     ['auxiliary processor n (name) is reserved but not installed', 'A variable was offered to a number that has no code behind it.'],
     ['no such auxiliary processor: n', 'A variable was offered to a number that is not registered.'],
     ['FILE ERROR: text', 'A file or component file operation failed: the file could not be opened, read, written, renamed or deleted, a handle is not open, or a component file is damaged. The C library\'s reason is added in parentheses.'],
     ['NET ERROR: text', 'A TCP/IP operation failed: an address could not be resolved, a connection was refused, a handle is not open, or sockets are not available on this platform.'],
     ['ACCESS ERROR: text', 'A private member was used outside its class.'],
     ['READONLY ERROR: text', 'A read only field or a property without a set function was assigned.'],
     ['METHOD ERROR: text', 'An :Override has nothing to override, or a base method is replaced without :Override.'],
     ['INTERFACE ERROR: text', 'A class does not supply a member of an interface it implements, for instance `B must have a public method Foo`.'],
     ['VALUE ERROR: text', 'An object, class or name that was used is gone: it was expunged, or never existed.']],
    [3800, 5838]);
  table('Syntax messages',
    ['Message', 'Meaning'],
    [['unexpected end of statement', 'A function or an operand is missing at the end.'],
     ['unexpected token near x', 'A word or symbol that does not fit at that place.'],
     ['undefined variable: x', 'x has no value.'],
     ['missing left argument for f', 'A dyadic-only function was used with one argument.'],
     ['missing function after op', 'An operator is not followed by a function.'],
     ['missing closing parenthesis, missing ] in index, missing } in lambda', 'Unbalanced brackets.'],
     ['unterminated string', 'A quote was opened and not closed.'],
     ['cannot assign to reserved word: x', 'x is the name of a primitive, an operator or a control word.'],
     ['name is already a function: x', 'x names a function, so it cannot be a variable.'],
     ['function returned no value', 'A function with no result was used where a value is needed.'],
     ['function is not monadic / not dyadic', 'A function was called with the wrong number of arguments.'],
     ['missing :End word in function  /  unbalanced :End word', 'The control words of a function do not pair up.'],
     ['a dop body must use alphaalpha', 'An operator definition does not use its operand.']],
    [3800, 5838]);

  // ======================================================== 10
  chapter('Restrictions and Known Problems');
  h2('Limits');
  ul([
    'Numbers are double-precision floating point. The largest magnitude is 7.0e75; anything beyond it is a DOMAIN ERROR. Whole numbers are exact up to 2^53, and integers used as indexes or counts must fit in 31 bits.',
    'An array has at most eight axes and at most 16,777,216 items. A line is at most 1,024 characters. Function calls may nest 200 deep, and `[]EA` 32 deep.',
    'Text is single-byte. Codes 0 to 255 can be made with `ucs`; what they print as depends on the terminal.'
  ]);
  h2('Language features that are not implemented');
  ul([
    '**Complex numbers** have no order and no factorial: see Chapter 6 for the functions that accept them. `floor`, `ceil` and `mod` of a complex number are not defined.',
    '**Objects** have no `:Trap`, `:Using`, `:Implements Constructor`, class-level `:Field` attributes beyond those of Chapter 11, multiple base classes, or operators in classes. A class that has live instances cannot be redefined. Events are delivered at once and not queued.',
    '**Selective assignment** works only with the structural functions listed in Chapter 3, applied to one variable of simple or nested items at the top level; it does not reach into items of a nested array (`(1 pick 2 pick v) <- ...`), and a computation such as `(v plus 1) <- 3` is refused.',
    '**Operators written with a function header**, as in APL2. The `dop` form replaces them, and its operands must be functions.',
    '**The P, R and S qualifiers of []FMT**, and the boxed display of nested arrays by the monadic form.',
    '**The control words** `:AndIf`, `:OrIf`, `:Trap`, `:With`, `:Section`, `:GoTo`, `:Access` and the rest of the Dyalog set.',
    '**The []FIO, []NA and []FHOLD interfaces.** Files, the screen and the network are reached through the auxiliary processors of Chapter 12, which also replace APL2\'s numbered AP interfaces; their commands are this interpreter\'s own.',
    '**Shared variables between programs.** The hooks connect a variable to a processor in the same process. `[]SVC`, `[]SVS`, `[]SVE`, `[]SVN` and the multi-user coupling states are not provided, and the value returned by `[]SVO` is always 1.',
    '**Dyalog primitives** for which no word exists: interval index (dyadic ⍸), stencil, variant, dyadic `⊂`, and left arguments to monadic operators other than those documented.',
    '**Other system names:** `[]LC`, `[]ML`, `[]FC`, `[]DR`, `[]CS`, `[]LX`, `[]ET`, `[]EM` and the rest not listed in Chapter 5.'
  ]);
  h2('Known problems');
  ul([
    'The words of the language are reserved. A program written before a word was added may have used it as a name: `first`, `left`, `right`, `log`, `rank`, `find`, `where`, `split`, `union`, `power`, `roll`, `deal`, `swap`, `repeat`, `until`, `format`, `execute`, `over`, `atop`, `amend`, `keyed`, `nest`, `ucs`, `zilch`, `dop` and `notmatch` are reserved.',
    '`[]DL` waits by spinning on the processor clock, which wastes the processor; it is meant for short pauses only.',
    '`)SAVE` records which variables are offered to which processor, but not what the processor holds: open files, sockets and component file handles are gone after `)LOAD`, and the program must open them again.',
    'A function that was interrupted by an error inside a `:For` loop leaves the loop’s variable set in the workspace, and a few bytes of memory for the loop are not released until the interpreter ends.',
    'The random number generator is a 32-bit xorshift. It gives the same numbers on every platform for the same `[]RL` but is not the generator of any other APL.',
    '`[]PP` counts decimal places, not significant digits, so very small numbers print as 0 unless `[]PP` is raised. Large numbers print in exponent form.'
      ]);
  h2('Still to do for VM/370');
  p('The language is complete enough for the port to begin, and the port is the next step. It needs: a deck-build step that folds the roughly one hundred source lines longer than 71 columns; a declaration of `setjmp` for GCCLIB under `__CMS__`; a single TEXT file for the loader; and a test on a Hercules VM/370 system. FILES and COMPFILE use only the C library (`fopen`, `fread`, `fwrite`), so they run wherever that does; the directory listing needs POSIX. TCP and SCREEN use a platform layer inside `p3d_proc.c` (BSD sockets and `termios`, guarded by `APL_HAVE_SOCKETS` and `APL_HAVE_TERMIOS`); without them the commands fail with a clear message. On CMS the TCP commands would be written against the TCP/IP for VM socket interface, and SCREEN against 3270 full-screen I/O, in the same two places.');

  // ======================================================== Appendices

  // ======================================================== 11
  chapter('Namespaces, Classes and Objects');
  p('Besides the classic workspace of variables and functions, the interpreter has the object features of Dyalog APL: **namespaces**, **classes**, **instances**, inheritance, **interfaces**, properties and events. They are optional. A program that does not use them runs exactly as before, and `)SAVE` and `)LOAD` keep everything, objects included.');
  h2('Namespaces');
  p('A namespace is a named container of variables, functions and other namespaces. It is written as a block that begins with `:Namespace Name` and ends with `:EndNamespace`; the lines between are ordinary assignments and function definitions. A name inside is reached with a dot: `Util.Twice 4`, `Util.k`. Namespaces nest, and a dotted name may be used on the left of an assignment, `Util.k <- 5`.');
  p('`:With ref` ... `:EndWith` makes the namespace or object `ref` the current scope for the lines in between. `:Include ns` inside a class copies the members of a namespace into it. `[]NS \'\'` makes an anonymous namespace and returns a reference to it, and `)CS Name` changes the current space for the session; `)CS` alone returns to the root.');
  h2('Classes');
  p('A class is a block `:Class Name` ... `:EndClass`. Inside it, `:Field` declares data, `:Property` declares a property, `:Event` declares an event, and ordinary `nabla` functions are the methods. A line such as `:Access Public` or `:Override` in front of a function changes how that one function is declared. `:Constructor` and `:Destructor` in front of a function mark it as the one that runs when an instance is made or discarded.');
  table('Class declarations',
    ['Line', 'Meaning'],
    [[':Class Name :Extends Base :Implements I1 I2', 'Start a class, optionally derived from Base and promising the interfaces I1, I2.'],
     [':Field [Public|Private] [Shared] [ReadOnly] name [<- value]', 'A data member. Private is the default. A Shared field is one value held by the class and seen by every instance. The value is evaluated when the instance is made.'],
     [':Access Public|Private [Shared]', 'Applies to the next function: whether it can be called from outside, and whether it works on the class instead of an instance.'],
     [':Override', 'Applies to the next function: it replaces a method of the base class. The name must exist in the base class, and a method that replaces one without :Override is a METHOD ERROR.'],
     [':Constructor', 'The next function runs when `[]NEW` makes an instance. Its arguments are the arguments of `[]NEW`.'],
     [':Destructor', 'The next function runs when the last reference to the instance is gone.'],
     [':Property [Public] Name', 'A property whose functions follow: `R<-get` and `set V`. Without a set function the property is read only.'],
     [':Event Name', 'Declares an event that instances can raise with `[]NQ`.'],
     [':Include ns', 'Copies the members of the namespace ns into the class.']],
    [4200, 5438], { mono: [0] });
  p('A Private member is visible only to the methods of the class that declares it. Public members are visible everywhere. A constructor and a destructor are always visible to derived classes so that they can call `[]BASE`.');
  h2('Instances');
  p('`[]NEW Account 100` makes an instance of Account and passes 100 to its constructor. The result is a **reference**: a scalar value that can be assigned, put in an array, passed to a function and compared with `match`. Two references are equal if they point at the same instance. A reference prints as `#.[Account]`; a class prints as `#.[Class Account]` and an anonymous namespace as `#.[Namespace]`. A member is selected with a dot: `a.Balance`, `a.Deposit 50`, `a.balance <- 7`. The form `(expr).member` selects a member of a computed reference.');
  p('Inside a method `[]THIS` is the instance, `[]BASE` is the base-class view of it, `[]ROOT` is the root and `[]PARENT` is the space that holds the class. A method called through a reference always runs the version of the instance\'s own class, so an overridden method is found even when the call is made from the base class.');
  p('When the last reference to an instance is dropped, by assignment, `[]EX`, the end of a function or `)CLEAR`, the destructor runs at the end of the statement that dropped it. An error in a destructor is reported but does not stop the session.');
  h2('Inheritance and interfaces');
  p('`:Extends Base` makes a derived class: it has all the fields and methods of Base and may add its own. A derived constructor calls the base constructor with `[]BASE.Name args`. A class has one base class. An interface is a block `:Interface IName` ... `:EndInterface` that lists `:Method`, `:Property` and `:Event` names; a class that says `:Implements IName` must have each of them, or the class is rejected with an INTERFACE ERROR. `obj []ISA Name` is 1 if obj is an instance of the class Name, of a class derived from it, or of a class that implements the interface Name.');
  h2('Properties and events');
  p('A property looks like a field: `t.F` calls the get function and `t.F <- 32` calls the set function with the new value. A property with no set function gives READONLY ERROR when assigned. An event is raised with `obj []NQ \'Event\' data`, or `[]NQ obj \'Event\' data`, which calls every handler that was attached with `obj []ON \'Event\' \'handler\'`. A handler is the name of a function, such as `\'w.Ring\'`, and is called with the data.  `[]ON` returns the instance, so it is usually assigned to a dummy variable.');
  h2('System functions for objects');
  table('Object system functions',
    ['Function', 'Result'],
    [['[]NEW cls args', 'A new instance of the class. `[]NEW cls` has no arguments for the constructor.'],
     ['[]NS names', 'A new anonymous namespace; with a text, a namespace of that name.'],
     ['[]CS ns', 'Changes the current space to ns, or to the root for an empty argument.'],
     ['[]NL k', 'The names of class k in the current space. `ns []NL k` lists those of ns or of an object. Class numbers: 2 variable, 3 function, 9.1 namespace, 9.2 instance, 9.4 class, 9.5 interface.'],
     ['[]NC names', 'The class number of each name: 0 undefined, 2, 3, 9.1, 9.2, 9.4, 9.5.'],
     ['[]NR name', 'The source of a method as a matrix, as `[]CR` gives it for a function.'],
     ['[]FIX lines', 'Defines a namespace, class or interface from its source lines, given as a matrix or a vector of texts. Returns the names defined.'],
     ['[]EX names', 'Removes variables, functions, namespaces or classes. A class that still has instances or derived classes cannot be removed (DOMAIN ERROR).'],
     ['obj []ISA name', '1 if obj is an instance of the class or interface name.'],
     ['[]CLASS obj', 'The class of an instance, as a reference to the class.'],
     ['[]INSTANCES cls', 'A vector of the live instances of the class.'],
     ['obj []DF text', 'Sets the text that is shown when obj is displayed.'],
     ['obj []ON ev handler', 'Attaches a handler to an event of obj.'],
     ['[]NQ obj ev data', 'Raises the event ev of obj with data.']],
    [3000, 6638], { mono: [0] });
  p('The commands `)CLASSES`, `)NSS` and `)INTERFACES` list what is defined, and `)CS` changes the current space. `)FNS` and `)VARS` list the members of the current space. `)SAVE` writes the namespaces, classes and interfaces as source and writes every instance with the values of its fields, and `)LOAD` and `)COPY` read them back.');
  h2('Objects as processors');
  p('An instance may serve as an auxiliary processor, so that a shared variable calls its methods. `n []PROC inst` registers the instance as processor number n (any positive number that no processor uses yet), and the class must have the public methods `Offer`, `Get`, `Set` and `Retract`. Afterwards `n []SVO \'Name\'` calls `inst.Offer \'Name\'`; each reference to Name calls `R<-Get \'Name\'`; an assignment calls `\'Name\' Set value`; and `[]SVR` calls `Retract`. Both forms work: the number `n` and the reference `inst` can be used where a processor is named. `)SAVE` records the processor and its shared variables.');
  h2('An example');
  p('The program below defines a namespace Bank with a class Account that has a shared count, a read only property and an event, and a class Savings derived from it. It makes two accounts, attaches a handler to the Overdrawn event and shows the property protecting the balance.');
  run('Objects (examples/oo_account.apl)', 'examples/oo_account.apl');


  // ======================================================== 12
  chapter('Auxiliary Processors and Communications');
  p('Chapter 5 describes shared variables: a variable is offered to a numbered processor, and the processor decides what a reference to it returns and what an assignment to it does. This chapter describes four processors that do real work: files, component files, the screen and TCP/IP. They share one protocol, so that once you know how to use one, you know how to use the others.');
  h2('The request and result protocol');
  p('You make a **request** by assigning to the shared variable: either a text that names a command, or a vector whose first item is that text and whose other items are the arguments. The processor carries the request out when the assignment is made, and the variable then holds the **result**, which you read like any variable. A request that cannot be carried out stops with an error, which `[]EA` can catch. Command names are not case sensitive.');
  snip('The protocol', "100 []SVO 'F'\nF <- 'open' 'protocol.tmp' 'w'\nh <- F\nF <- 'writeline' h 'one line'\nF\nF <- 'close' h\nF <- 'size' 'protocol.tmp'\nF\nF <- 'delete' 'protocol.tmp'\n('execute ''F <- ''''bogus''''''' []EA '[]DM')");
  p('A request is a strand, so a text argument is a quoted text, a number is a number, and an argument that is itself an expression must be put in parentheses: `F <- \'writeline\' h (\'total \' cat format n)`. Results that mean “nothing there” (the end of a file, no data from a socket) are `zilch`, an empty numeric vector; an empty text is a real empty line.');
  h2('AP 100: FILES');
  table('FILES commands',
    ['Request', 'Result'],
    [['\'open\' name [mode]', 'A handle (1 to 32). The mode is r (the default), w, a, r+, w+ or a+. Files are read and written byte for byte.'],
     ['\'close\' h', '1.'],
     ['\'readline\' h', 'The next line without its line end, or zilch at the end of the file.'],
     ['\'read\' h n', 'Up to n characters, or zilch at the end of the file.'],
     ['\'write\' h text  \'writeline\' h text', 'The number of characters written; `writeline` adds a line end.'],
     ['\'seek\' h pos [whence]', 'Moves to pos (counted from the start; whence 1 is from the current place, 2 from the end). Returns the new place.'],
     ['\'tell\' h  \'eof\' h  \'flush\' h', 'The place; 1 if the end of the file has been reached; 1.'],
     ['\'size\' h  or  \'size\' name', 'The size of the file in bytes.'],
     ['\'readall\' name  \'lines\' name', 'The whole file as one text vector (line ends are the character with code 10), or as a matrix with a row for each line.'],
     ['\'exists\' name  \'delete\' name  \'rename\' old new', '1 or 0; 1; 1.'],
     ['\'dir\' [pattern]', 'A matrix of the file names that match the pattern (`*` and `?`); sorted. Not on every platform.'],
     ['\'handles\'', 'The open handles.']],
    [3600, 6038], { mono: [0] });
  h2('AP 210: COMPFILE');
  p('A component file keeps numbered APL values, of any shape and depth, in a file. The file is text, so it can be moved between machines and read with an editor. Components are numbered from 1. An object reference cannot be stored.');
  table('COMPFILE commands',
    ['Request', 'Result'],
    [['\'create\' name  \'open\' name', 'A handle (1 to 16). `create` makes an empty file and fails if the file is there.'],
     ['\'close\' h', '1.'],
     ['\'append\' h value', 'The number of the new component.'],
     ['\'read\' h n', 'Component n.'],
     ['\'replace\' h n value', 'n. The file is rewritten.'],
     ['\'drop\' h', 'Removes the last component; returns the new count.'],
     ['\'count\' h', 'The number of components.'],
     ['\'exists\' name  \'erase\' name', '1 or 0; 1.']],
    [3600, 6038], { mono: [0] });
  snip('Component files', "210 []SVO 'C'\nC <- 'create' 'demo.cf'\nh <- C\nC <- 'append' h (2 3 rho iota 6)\nC <- 'append' h ('ab' (1 2 3) (4J5 6))\nC <- 'append' h 'plain text'\nC <- 'count' h\nC\nC <- 'read' h 2\nC\nC <- 'replace' h 3 'changed'\nC <- 'close' h\nC <- 'open' 'demo.cf'\nh <- C\nC <- 'read' h 3\nC\nC <- 'close' h\nC <- 'erase' 'demo.cf'");
  h2('AP 124: SCREEN');
  p('SCREEN controls a terminal that understands ANSI escape sequences (VT100, xterm and nearly everything since). Rows and columns are counted from 1 at the top left. It writes to standard output and reads keys from standard input, so a program can be tested with a file as input; on a terminal it switches the keyboard to character mode for the keys, and restores it at the end of the session.');
  table('SCREEN commands',
    ['Request', 'Effect and result'],
    [['\'clear\'', 'Erases the screen and moves the cursor to the top left.'],
     ['\'put\' row col text', 'Writes the text at that place; returns its length.'],
     ['\'cursor\' row col', 'Moves the cursor.'],
     ['\'attr\' n ...', 'Sets the display attributes: 0 normal, 1 bold, 4 underline, 7 reverse, 30 to 37 colours, 40 to 47 background colours. With no numbers: normal.'],
     ['\'box\' row col h w', 'Draws a frame h rows high and w columns wide with + - and |.'],
     ['\'size\'', 'The number of rows and columns: from the environment variables LINES and COLUMNS, otherwise 24 and 80.'],
     ['\'getkey\' [ms]', 'Waits for a key and returns its code; 0 if the time out ran out, −1 at the end of the input. Enter is 13, backspace 8, escape 27, and 1001 up, 1002 down, 1003 right, 1004 left, 1005 home, 1006 end, 1007 delete, 1008 page up, 1009 page down.'],
     ['\'input\' row col len [text]', 'Edits a field of len characters, starting with the text; returns what was entered. Enter ends it; escape clears it. Without a terminal, a line of input is read.'],
     ['\'bell\'  \'flush\'  \'hide\'  \'show\'  \'reset\'', 'Sounds the bell; writes pending output; hides or shows the cursor; restores the normal attributes, the cursor and the keyboard.']],
    [3600, 6038], { mono: [0] });
  h2('AP 160: TCP');
  p('TCP gives a program the same sockets that other languages use. A connection is a handle (1 to 32). The commands that wait take a time out in milliseconds; without one they wait for ever. A server is a loop around `accept`, `recvline` and `send`; a client is `connect`, `send` and `recvline`.');
  table('TCP commands',
    ['Request', 'Result'],
    [['\'connect\' host port', 'A handle for a connection to the host (a name or a numeric address).'],
     ['\'listen\' port [address]', 'A handle that is waiting for clients on that port; port 0 takes any free port, and `port` tells which.'],
     ['\'accept\' h [ms]', 'The handle of the next client, or 0 if the time out ran out.'],
     ['\'send\' h text', 'The number of characters sent.'],
     ['\'recv\' h n [ms]', 'Up to n characters, as soon as some have arrived. Zilch if none came within the time out, or if the other side has closed.'],
     ['\'recvline\' h [ms]', 'One line without its line end (CR LF or LF); zilch as for `recv`. A partial line is kept for the next call.'],
     ['\'poll\' h [ms]', '1 if there is something to read (or a client to accept), else 0.'],
     ['\'status\' h', '1 once the other side has closed and everything has been read.'],
     ['\'port\' h  \'peer\' h', 'The local port number; the address and port of the other side, as text.'],
     ['\'resolve\' host', 'The numeric address of a host name, as text.'],
     ['\'close\' h  \'handles\'', '1; the open handles.']],
    [3600, 6038], { mono: [0] });
  p('The program below is a complete exchange in one session: a server listens on a free port, a client connects to it, the server answers. On the loopback address the connection completes without a second process, which makes it a good test; with two sessions, the same requests make a real server and a real client.');
  run('TCP on the loopback (examples/net_echo.apl)', 'examples/net_echo.apl');
  p('TCP is written for POSIX sockets, which Linux, the BSD systems and macOS provide. On a platform without them every TCP request is a NET ERROR that says so. Because the protocol is the same, a port to another stack changes only the platform layer in `p3d_proc.c`.');
  h2('Writing a processor with the same protocol');
  p('A processor needs only a `set` hook. It receives the request, parses it with `rq_parse` (the command is lower case in `q.cmd`; `rq_str`, `rq_int` and `rq_num` fetch the arguments), does the work, and returns the result as a new value; whatever it returns becomes the value of the variable. Register it in `ap_init_extra` with a free number. Raise errors with `rt_error`, and do not keep an `Arr *` in a static variable: the interpreter reclaims values that no variable holds after an error.');
  show('A processor in six lines', "static Arr *echo_set(const char *var, Arr *val)\n{\n  Rq q;\n  (void)var;\n  rq_parse(val, &q);                 /* q.cmd, q.n arguments */\n  return res_text(q.cmd, (int)strlen(q.cmd));\n}\n\n/* in ap_init_extra: */\nap_register(300, \"ECHO\", 1, NULL, NULL, echo_set, NULL);");

  chapter('Appendix A. Commands', { plain: true, appendix: true });
  table('Commands',
    ['Command', 'Action'],
    [[')VARS', 'List the variables.'],
     [')FNS', 'List the functions, including named lambdas and operators.'],
     [')PRIMS', 'List the words, operators, system names and control words.'],
     [')PROCS', 'List the auxiliary processors and the variables shared with them.'],
     [')LIB', 'List the saved workspaces in the current directory.'],
     [')WSID [name]', 'Show or set the workspace name.'],
     [')CLEAR', 'Remove all variables and functions; retract shared variables; set the origin to 1 and the precision to 6.'],
     [')SAVE [name]', 'Save the workspace as the source file name.apl.'],
     [')LOAD name', 'Clear the workspace and load a saved one.'],
     [')COPY name [objects]', 'Bring everything, or the named objects, from a saved workspace.'],
     [')ERASE names', 'Erase variables or functions.'],
     [')HELP  or  )?', 'Print a summary.'],
     [')QUIT  )EXIT  )OFF', 'End the session.']],
    [3000, 6638], { mono: [0] });
  p('A command is not case sensitive. In a script, a command line is executed as it is read.');
  chapter('Appendix B. The Repository', { plain: true, appendix: true });
  table('The repository',
    ['Path', 'Contents'],
    [['src/', 'The six parts of the interpreter and `apl.c`'],
     ['apl_all.c', 'The single-file form, generated by `tools/amalg.sh`'],
     ['Makefile', 'Targets all, test and clean'],
     ['examples/', 'The example programs'],
     ['tests/', '`run.sh` and the scenario tests in `tests/scen`, with expected output'],
     ['tools/', '`amalg.sh`'],
     ['docs/', 'This manual, `APL-User-Guide.docx`, and the program that generates it'],
     ['ref/', 'The REXX prototype (`apl.rex`), its documentation and reference outputs']],
    [2400, 7238], { mono: [0] });
  p('To regenerate this book, build the interpreter and run `node docs/build_manual.js` from the top directory. The figures are produced by running the interpreter, so they cannot go out of date without the next build showing it.');
  chapter('Appendix C. Differences from APL2 and Dyalog APL', { plain: true, appendix: true });
  p('The first figure lists the differences in how a program is written. The second lists differences in behavior that a reader who knows APL might trip over.');
  table('Differences in notation',
    ['APL2 and Dyalog', 'This interpreter'],
    [['Glyphs, such as ⍴ ⍳ ⌽', 'Words: `rho`, `iota`, `reverse`; see Chapter 6'],
     ['`←`  assignment, `→`  branch', '`<-` and `->`'],
     ['`∇` to define a function', '`nabla`, or `edit`'],
     ['`⎕` before a system name', '`[]` before it: `[]IO`, `[]EA`'],
     ['`⍝` comment', '`#` or `lamp`'],
     ['`⍬` zilch', '`zilch`'],
     ['Operators between their operands: `+/v`, `f¨v`', 'Operators before them: `reduce plus v`, `each f v`'],
     ['`*` is power, `/` is reduce or compress, `|` is residue', '`*` is times, `/` is divide, `mod` is residue; `power`, `reduce`, `compress`'],
     ['`⍺`, `⍵`, `∇` in a dfn; statements separated by `⋄`', '`alpha`, `omega`, `self`; statements separated by `;`; guards as `cond : value`'],
     ['Dop operands `⍺⍺` `⍵⍵`', '`alphaalpha`, `omegaomega`'],
     ['`:If` ... and `:For` are Dyalog only', 'The same words, any case, one per line']],
    [4800, 4838]);
  table('Differences in behavior',
    ['Topic', 'Difference'],
    [['Complex numbers', 'Supported, written `3J4`; the J form is also what is printed. No complex floor, ceiling, residue or factorial.'],
     ['`[]FMT`', 'The common APL2 phrases and qualifiers (I F E G A X T, B C L Z K M N); the P, R and S qualifiers are missing.'],
     ['Selective assignment', 'Structural selections of one variable; see Chapter 3.'],
     ['Auxiliary processors', 'The numbered processors 100, 124, 160 and 210 are this interpreter\'s own and use a request/result protocol (Chapter 12); they are not compatible with the IBM processors of the same numbers.'],
     ['`[]PP`', 'Counts decimal places, not significant digits.'],
     ['Nested display', 'No boxes or indentation: an array item has a blank before it.'],
     ['`keyed`, `amend`', 'Keyed takes a vector of keys. Amend works on vectors, with an index list only.'],
     ['`reduce` and `scan`', 'Both work along the last axis unless `[k]` is given; `n reduce` is the n-wise reduction.'],
     ['`rank`', 'Takes one to three numbers, as in Dyalog.'],
     ['Random numbers', '`roll` and `deal` use a 32-bit xorshift generator.'],
     ['`[]SVO`', 'Always returns 1 for an accepted offer; there are no coupling degrees.'],
     ['Names', 'Case sensitive. Reserved words cannot be used as names.'],
     ['Output of numbers', 'A negative number is printed with a minus sign; very large numbers are printed with an exponent.'],
     ['Objects', 'Dyalog style: `:Class`, `[]NEW`, `[]NS`, `[]FIX`. A class is one block in the session, not an editor session; there are no Dyalog-style `:Trap`, `:Using`, `:Implements Constructor`, or `:Field` attributes beyond those in Chapter 11.'],
     ['Numbers beyond 7.0e75', 'DOMAIN ERROR on every machine, to match the VM/370 range.']],
    [2400, 7238]);
  h2('Changes from the REXX prototype');
  p('The interpreter began as a prototype written in Regina REXX. The C version keeps its commands, its editor and its programs, and differs from it in the following ways, which show in the examples: `mod` follows APL, so that `a mod b` is the remainder of b divided by a; the reduction of an empty array gives the identity element instead of an error; `0 divide 0` is 1; arrays may be nested and have up to eight axes; and the numbers have a limited range.');
};
