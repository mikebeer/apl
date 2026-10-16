# AP 124 SCREEN: output sequences (no keyboard needed)
124 []SVO 'S'
S <- 'size'
rho S
S <- 'clear'
S <- 'put' 2 5 'Hello'
S <- 'attr' 1 31
S <- 'box' 1 1 3 6
S <- 'attr'
S <- 'cursor' 4 1
S <- 'bell'
S <- 'hide'
S <- 'show'
S <- 'reset'
('execute ''S <- ''''box'''' 1 1 1 1''' []EA '[]DM')
('execute ''S <- ''''frobnicate''''''' []EA '[]DM')
