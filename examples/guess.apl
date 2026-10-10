# guess.apl - a guessing game; answers come from standard input
[]RL <- 12345
secret <- roll 100
tries <- 0

nabla PLAY;G
LOOP: 'Your guess (1-100)?'
G<-[]
tries <- tries + 1
-> (G=secret) compress WIN
-> (G<secret) compress LOW
'lower'
-> LOOP
LOW: 'higher'
-> LOOP
WIN: 'Got it in ' cat (format tries) cat ' tries'
nabla

PLAY
