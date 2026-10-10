# arrays.apl - shape, reshape, indexing and axes
v <- 10 20 30 40 50
rho v
m <- (3 4) rho iota 12
m
rho m
v[2 4]
m[2;3]
m[2;]
m[;1]
m[1 3;2 3]
m[2;3] <- 0
m
reverse m
reverse[1] m
1 rotate m
reduce plus m
reduce plus[1] m
transpose m
(2 3 4) rho iota 24
