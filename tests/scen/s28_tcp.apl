# AP 160 TCP: a server and a client in one process over the loopback
160 []SVO 'N'
N <- 'listen' 0 '127.0.0.1'
srv <- N
N <- 'port' srv
port <- N
port > 0
N <- 'connect' '127.0.0.1' port
cli <- N
N <- 'accept' srv 2000
conn <- N
conn > 0
N <- 'send' cli 'hello server'
N
N <- 'recv' conn 100 2000
N
N <- 'send' conn 'line one'
N <- 'send' conn ([]TC[3] cat 'x')
N <- 'recvline' cli 2000
N
N <- 'recvline' cli 100
rho N
N <- 'poll' cli 50
N
N <- 'send' cli 'abc'
N <- 'close' cli
N <- 'recv' conn 10 2000
N
N <- 'recv' conn 10 2000
rho N
N <- 'status' conn
N
N <- 'peer' conn
(1 take N) = '1'
N <- 'resolve' 'localhost'
N
N <- 'accept' srv 50
N
N <- 'handles'
N
N <- 'close' conn
N <- 'close' srv
('execute ''N <- ''''connect'''' ''''127.0.0.1'''' 1''' []EA '[]DM')
('execute ''N <- ''''send'''' 99 ''''x''''''' []EA '[]DM')
