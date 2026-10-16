# AP 210 COMPFILE: numbered APL values in a file
210 []SVO 'C'
C <- 'create' 'zz_cf.dat'
h <- C
C <- 'append' h 'hello world'
C <- 'append' h (3 1J2 4)
C <- 'append' h (2 3 rho iota 6)
C <- 'append' h ('ab' (1 2 3) (4 5))
C <- 'count' h
C
C <- 'read' h 1
C
C <- 'read' h 2
C
C <- 'read' h 3
C
C <- 'read' h 4
C
C <- 'replace' h 1 'changed'
C <- 'close' h
C <- 'open' 'zz_cf.dat'
h <- C
C <- 'count' h
C
C <- 'read' h 1
C
C <- 'read' h 4
C
C <- 'drop' h
C
C <- 'close' h
('execute ''C <- ''''open'''' ''''zz_nothere.dat''''''' []EA '[]DM')
('execute ''C <- ''''create'''' ''''zz_cf.dat''''''' []EA '[]DM')
C <- 'open' 'zz_cf.dat'
h <- C
('execute ''C <- ''''read'''' h 99''' []EA '[]DM')
C <- 'close' h
C <- 'erase' 'zz_cf.dat'
