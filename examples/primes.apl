# primes.apl -- classic APL "sieve" idiom, ported to word-form operators
#
# Find all primes up to N using the mod-outer-product idiom:
#   1. build candidates 2..N
#   2. for each candidate c, it is prime if no smaller candidate d>1
#      divides it evenly (other than itself)
# This version uses the compress-driven approach instead of the full
# outer-product sieve, which keeps the example easy to follow.

n <- 30
cand <- 1 drop (iota n)         # 2 3 4 ... n

# Since this dialect has no explicit loop construct, we compute the
# "is prime" flag vector via a nested outer-product / reduce idiom
# instead of a loop:
#   for each c in cand, c is prime iff exactly one d in cand divides c
#   (that divisor being c itself).

divtable <- outer mod cand cand     # divtable[i,j] = cand[j] residue after dividing by cand[i]
iszero   <- divtable = 0            # 1 where cand[i] divides cand[j]
counts   <- reduce plus[1] iszero   # number of divisors (within cand) of each cand[j]
isprime  <- counts = 1              # exactly one divisor among candidates -> prime

primes <- isprime compress cand
primes
