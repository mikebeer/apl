# basic vector / iota / rho
v <- iota 5
v
rho v
reduce plus v
2 take v
-2 take v
2 drop v
reverse v

# matrices
m <- (2 3) rho (1 2 3 4 5 6)
m
rho m
transpose m
2 rotate v
v cat (10 20)
(1 0 1 0 1) compress v
v index 3
m index 2
m index (2 3)
reduce plus m
scan plus v
outer times v v
(iota 3) matmul (iota 3)
