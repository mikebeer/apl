# AP 100 FILES: text files, seek, lines, directory, errors
100 []SVO 'F'
F <- 'open' 'zz_test.txt' 'w'
h <- F
F <- 'writeline' h 'first line'
F <- 'writeline' h 'second'
F <- 'write' h 'no newline'
F <- 'close' h
F <- 'exists' 'zz_test.txt'
F
F <- 'size' 'zz_test.txt'
F
F <- 'open' 'zz_test.txt'
h <- F
F <- 'readline' h
F
F <- 'readline' h
F
F <- 'eof' h
F
F <- 'readline' h
F
F <- 'readline' h
rho F
F <- 'eof' h
F
F <- 'seek' h 0
F <- 'read' h 5
F
F <- 'tell' h
F
F <- 'close' h
F <- 'lines' 'zz_test.txt'
F
rho F
F <- 'readall' 'zz_test.txt'
rho F
F <- 'rename' 'zz_test.txt' 'zz_t2.txt'
F <- 'delete' 'zz_t2.txt'
F <- 'exists' 'zz_t2.txt'
F
('execute ''F <- ''''open'''' ''''nonexistent_dir/x'''' ''''r''''''' []EA '[]DM')
('execute ''F <- ''''bogus''''''' []EA '[]DM')
('execute ''F <- 5''' []EA '[]DM')
F <- 'handles'
rho F
# directory listing with a wildcard
F <- 'open' 'zz_d1.txt' 'w'
F <- 'close' 1
F <- 'open' 'zz_d2.txt' 'w'
F <- 'close' 1
F <- 'dir' 'zz_d*'
F
F <- 'delete' 'zz_d1.txt'
F <- 'delete' 'zz_d2.txt'
