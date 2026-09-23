# tcping
A terminal tool to ping a TCP port.


The next is an example to ping a TCP.

```
$ tcping github.com 443
TCPING github.com (20.205.243.166) 443
TCP 20.205.243.166 443: seq=1 time=90.001 ms
TCP 20.205.243.166 443: seq=2 time=88.103 ms
TCP 20.205.243.166 443: seq=3 time=88.476 ms
TCP 20.205.243.166 443: seq=4 time=89.535 ms
TCP 20.205.243.166 443: seq=5 time=86.942 ms
TCP 20.205.243.166 443: seq=6 time=86.388 ms
TCP 20.205.243.166 443: seq=7 time=89.389 ms
TCP 20.205.243.166 443: seq=8 time=89.543 ms
TCP 20.205.243.166 443: seq=9 time=89.374 ms
TCP 20.205.243.166 443: seq=10 time=86.122 ms
TCP 20.205.243.166 443: seq=11 time=87.664 ms
^C
--- github.com 443 TCP statistics ---
11 packets transmitted, 11 packets received, 0.00% packet loss
round-trip min/avg/max/stddev = 86.122/88.322/90.001/1.902
```
