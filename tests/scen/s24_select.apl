# selective assignment: (selection v) <- value
v <- 1 2 3 4 5 6
(2 take v) <- 0
v
(2 drop v) <- 9
v
(reverse v) <- 10 20 30 40 50 60
v
((v > 40) compress v) <- 0
v
m <- 3 3 rho iota 9
(1 3 take m) <- 0
m
(ravel m) <- iota 9
m
(transpose m) <- 3 3 rho 0
m
first m
(first v) <- 99
v
((neg 2) take v) <- 7 8
v
(2 rotate v) <- 1 2 3 4 5 6
v
t <- 'hello'
(1 take t) <- 'J'
t
(2 take t) <- 5
t
(3 pick v) <- 33
v
# errors
('execute ''(v plus 1) <- 3''' []EA '[]DM')
('execute ''(7 take v) <- 3''' []EA '[]DM')
('execute ''(2 take v) <- 1 2 3''' []EA '[]DM')
('execute ''(2 take w) <- 1''' []EA '[]DM')
v <- 1 2 3 4 5 6
(2 take v) <- 'ab'
v
# inside a function
nabla Z<-CLIP V
(V > 3) compress V
nabla
nabla Z<-ZAP V
(1 take V) <- 0
Z<-V
nabla
ZAP 5 6 7
x <- 4 5 6
(1 drop x) <- 0
x
