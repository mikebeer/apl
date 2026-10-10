# lambda.apl - anonymous functions with alpha and omega
sq <- {omega times omega}
sq 5
sq iota 4
2 {alpha plus omega} 3
each {omega plus 1} 1 2 3
reduce {alpha plus omega} 1 2 3 4
fac <- {(omega = 0) : 1 ; omega times self omega minus 1}
fac 10
fib <- {(omega < 2) : omega ; (self omega minus 1) plus self omega minus 2}
fib 15
mean <- {(reduce plus omega) divide tally omega}
mean 2 4 9
f <- {t <- omega times 2 ; t plus 1}
f 4
)fns
