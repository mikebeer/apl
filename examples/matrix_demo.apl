# matrix_demo.apl -- reshape, transpose, matmul, reduce over rows/cols

# Build a 3x3 matrix from 1..9
a <- (3 3) rho (iota 9)
a

# Transpose it
at <- transpose a
at

# Row sums and column sums
rowsums <- reduce plus a
rowsums
colsums <- reduce plus at
colsums

# Identity-ish check: a matmul-multiplied by a 3-vector of ones gives rowsums
ones3 <- (3 3) rho 1
ones3

# Matrix multiply: a (3x3) times its own transpose (3x3) -> 3x3
gram <- a matmul at
gram

# Take the top-left 2x2 corner
(2 2) take a

# Reverse each row (monadic reverse on a matrix)
reverse a

# Rotate every row left by 1
1 rotate a
