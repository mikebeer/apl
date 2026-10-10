# operators.apl - reduce, scan, each, outer, inner, rank and friends
reduce plus iota 10
reduce times iota 5
scan plus iota 5
each neg 1 2 3
1 2 3 each plus 10 20 30
1 2 3 outer times 1 2 3
(1 2 3) inner plus times (4 5 6)
m <- (2 2) rho 1 2 3 4
m inner plus times m
2 reduce plus 1 2 3 4 5
swap minus 3
2 swap minus 10
repeat neg 3 5
rank reverse 1 (2 3) rho iota 6
until {omega times 2} {alpha > 100} 1
