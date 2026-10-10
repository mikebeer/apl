y <- (1 2) (3 4 5) 'abc'
y
depth y
tally y
(1 2) match (1 2)
(1 2) match (1 3)
enclose 5
depth enclose 1 2 3
disclose (1 2) (3 4 5)
first (7 8) 9
m <- (2 2) rho (1 2) 3 'x' (4 5 6)
m
rho m
2 member 1 2 3
enlist (1 2) (3 (4 5))
1 pick y
(enclose 1 2) pick (2 2) rho y
reduce cat (1 2) (3) (4 5)
scan plus (1 2) (3 4) (5 6)
each reverse y
each rho (1 2) 'abc'
y cat 9
n <- (1) rho enclose 1 2 3
n
rho n
)SAVE nest_ws
)CLEAR
)LOAD nest_ws
y
m
n
disclose 1 2 3
'(1 2) plus (3 4 5)' []EA '''caught'''
