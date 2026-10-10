# userop.apl - operators of your own: a dop uses alphaalpha and omegaomega
twice <- dop { alphaalpha alphaalpha omega }
twice neg 5
twice {omega times 2} 3

# two operand functions: apply g first, then f, to every item
mapsum <- dop { reduce alphaalpha each omegaomega omega }
mapsum plus abs neg 1 2 3

# a recursive helper written as a lambda still works as an operand
sq <- {omega times omega}
twice sq 3
1 twice plus 10
# the Dyalog-style composition operators are built in
(3 4) over plus neg 1 2
atop neg {reduce plus omega} 1 2 3
