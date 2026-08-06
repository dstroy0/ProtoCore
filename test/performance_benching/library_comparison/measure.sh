#!/usr/bin/env bash
# Measure one benchmark server. Usage: ./measure.sh <board-ip> [label]
# Needs curl + h2load (nghttp2) on the client. Run ONE board at a time (see README - RF hygiene).
set -u
H="${1:?usage: ./measure.sh <board-ip> [label]}"
LABEL="${2:-$H}"
echo "################  $LABEL  ($H)  ################"

echo "-- single-request RTT, new connection each (curl internal timings, n=40) --"
: >/tmp/bench_rtt.dat
for i in $(seq 40); do
  curl -s -o /dev/null -w "%{time_connect} %{time_starttransfer} %{time_total}\n" "http://$H/" >>/tmp/bench_rtt.dat 2>/dev/null
done
awk '{c+=$1*1000; s+=($2-$1)*1000; t+=$3*1000; n++} END{printf "   connect ~%.1f ms | server(ttfb-connect) ~%.1f ms | total ~%.1f ms\n", c/n, s/n, t/n}' /tmp/bench_rtt.dat

echo "-- keep-alive throughput, small / (h2load --h1, n=3000) --"
for c in 1 4; do
  O=$(h2load --h1 -n 3000 -c "$c" "http://$H/" 2>&1)
  echo "   -c$c: $(echo "$O" | grep -oiE '[0-9.]+ req/s' | head -1)  ($(echo "$O" | grep -oiE '[0-9]+ succeeded' | head -1), $(echo "$O" | grep -oiE '[0-9]+ failed' | head -1))"
done

echo "-- payload throughput (h2load --h1) --"
for p in /4k /64k; do
  O=$(h2load --h1 -n 500 -c 1 "http://$H$p" 2>&1)
  echo "   $p: $(echo "$O" | grep -oiE '[0-9.]+ req/s' | head -1), $(echo "$O" | grep -oiE '[0-9.]+[KM]B/s' | head -1)"
done
