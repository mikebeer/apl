sq <- {omega times omega}
sq 5
sq iota 4
{omega plus 1} 3
2 {alpha plus omega} 3
each {omega times 2} 1 2 3
reduce {alpha plus omega} 1 2 3 4
fac <- {(omega = 0) : 1 ; omega times self omega minus 1}
fac 5
fib <- {(omega < 2) : omega ; (self omega minus 1) plus self omega minus 2}
fib 10
f <- {t <- omega times 2 ; t plus 1}
f 4
1 2 3 each plus 10 20 30
(1 2 3) inner plus times (4 5 6)
m <- (2 2) rho 1 2 3 4
m inner plus times m
swap minus 3
2 swap minus 10
repeat {omega times 2} 5 1
2 repeat {alpha plus omega} 3 10
(2 3) outer times 4 5
1 2 3 outer plus 10 20
3 reduce plus 1 2 3 4 5
2 reduce {alpha minus omega} 1 4 9 16
(2 2) rho 1 2 3 4
rank reverse 1 m
rank reverse 2 m
(1 2) rank cat 0 (3 4)
m rank {alpha plus omega} 1 (10 20)
rank {reduce plus omega} 1 m
rank {tally omega} 0 m
rank {reverse omega} (1) (iota 3)
)fns
