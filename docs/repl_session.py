# Records an interactive session through a pty so that typed input is echoed.
import os, pty, sys, time, select
lines = ["x <- iota 5", "x times x", "reduce plus x", "nabla Z<-CUBE N", "Z<-N times N times N", "nabla",
         "CUBE 3", "CUBE iota 4", "1 divide 0", "m <- (2 3) rho iota 6", "transpose m", ")vars", ")quit"]
pid, fd = pty.fork()
if pid == 0:
    os.execv(sys.argv[1], [sys.argv[1]])
out = b""
def drain(t=0.3):
    global out
    while True:
        r, _, _ = select.select([fd], [], [], t)
        if not r: break
        try: d = os.read(fd, 4096)
        except OSError: break
        if not d: break
        out += d
drain(0.5)
for l in lines:
    os.write(fd, (l + "\n").encode()); drain()
text = out.decode("latin1").replace("\r\n", "\n").replace("\r", "")
sys.stdout.write(text)
