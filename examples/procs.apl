# procs.apl - shared variables and the demonstration processors
)PROCS
# AP 2 turns whatever text is assigned into capitals
2 []SVO 'MSG'
MSG <- 'hello'
MSG
# AP 3 yields the next integer every time the variable is referenced
3 []SVO 'N'
N
N
N <- 100
N
[]SVQ 3
[]SVR 'N'
N <- 5
N
