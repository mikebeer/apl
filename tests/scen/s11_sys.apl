format 3.5 4
2 format 3.14159 2.5
(8 3) format 3.14159
(6 2 8 1) format (2 2) rho 1.5 2.25 3 4
execute '2 plus 3'
execute 'zz <- 7'
zz
[]CT <- 1e-5
1.000001 = 1
[]CT <- 1e-13
[]NC 'zz'
[]NC 'nothere'
[]FX ('r<-dbl x' 'r<-x plus x')
dbl 21
[]CR 'dbl'
[]EX 'zz'
[]NC 'zz'
[]A
sq <- {omega times omega}
v <- 1 2 3
)SAVE s11tmp
)CLEAR
)COPY s11tmp sq
sq 6
)COPY s11tmp v nope
v
