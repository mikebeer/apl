# negatives, arithmetic, comparisons
a <- 3 -2 5 -7
a
neg a
abs a
a = 3 -2 5 -7
a > 0
2 * a
10 / 4

# reshape cycling
(3 3) rho (1 2 3)

# reduce/scan with different ops
reduce max (5 3 9 1 7)
reduce min (5 3 9 1 7)
scan times (1 2 3 4)

# each
each abs (3 -4 5 -6)

# rotate negative
-1 rotate (1 2 3 4 5)

# drop from matrix
(1 1) drop ((3 3) rho (1 2 3 4 5 6 7 8 9))

# boolean logic
1 and 0
1 or 0
not 0
