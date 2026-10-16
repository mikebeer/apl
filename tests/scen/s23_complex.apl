# complex numbers: literals, arithmetic, functions, arrays
z <- 3J4
z
abs z
+z
- z
z plus 1J-1
z times z
z divide 1J1
signum z
recip 0J1
(neg 4) power 0.5
(neg 8) power 1 divide 3
exp 0J1
ln neg 1
3 log 0J9
1 2J3 4
2 times 1 2J3 4
rho 1 2J3 4
z = 3J4
z <> 3J5
z = 'a'
# circle: 9 real, 10 magnitude, 11 imaginary, 12 phase
9 circle z
10 circle z
11 circle z
12 circle 1J1
1 circle 1J1
2 circle 0J1
-1 circle 2
-2 circle 2
-3 circle 0J0.5
# real arguments that need a complex answer
4 circle 0.5
0 circle 2
ln neg 5
# arrays
v <- 1J1 2 3J-2
reverse v
v[2]
v[2] <- 7J7
v
2 3 rho 1J1 2 3 4 5 6J-1
transpose 2 3 rho 1J1 2 3 4 5 6
reduce plus v
scan times v
each abs v
outer times 1J1 2
m <- 2 2 rho 1J1 2 3 4J-1
m matmul m
m inner plus times m
first v
v match v
v match 1 2 3
unique 1J1 1J1 2 2
1J1 2 indexof 2
# errors
('z < 1' []EA '[]DM')
('floor z' []EA '[]DM')
('gradeup v' []EA '[]DM')
('z divide 0' []EA '[]DM')
('1e70 times 1e70J1' []EA '[]DM')
# literals in text and execute
execute '2J3 times 2'
format 3J4
# save and load
)SAVE s23ws
)CLEAR
)LOAD s23ws
v
z
