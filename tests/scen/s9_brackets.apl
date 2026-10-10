v <- 10 20 30 40 50
v[2]
v[2 4]
v[(2 2) rho 1 5]
m <- (3 3) rho iota 9
m[2;3]
m[2;]
m[;1]
m[1 3;1 3]
1 2 3[2]
v[2] <- 99
v
m[2;2] <- 0
m
m[;1] <- 7 8 9
m
v[1 2] <- 0
v
s <- 'hello'
s[1] <- 'J'
s
s[1 2] <- 'ab'
s
w <- (1 2) (3 4)
w[2]
w[1] <- 'x'
w
reduce plus[1] m
reduce plus m
scan plus[1] m
reverse[1] m
1 rotate[1] m
(1 2) cat[1] (3 4)
