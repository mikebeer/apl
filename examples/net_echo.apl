# TCP/IP: an echo server and a client in one session, over the loopback
160 []SVO 'N'
N <- 'listen' 0 '127.0.0.1'
server <- N
N <- 'port' server
port <- N
N <- 'connect' '127.0.0.1' port
client <- N
N <- 'accept' server 2000
peer <- N
N <- 'send' client 'ping'
N <- 'recv' peer 100 2000
request <- N
request
reply <- 'echo: ' cat request
N <- 'send' peer reply
N <- 'recv' client 100 2000
N
N <- 'close' client
N <- 'close' peer
N <- 'close' server
