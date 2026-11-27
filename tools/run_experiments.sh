#!/bin/sh
# Run the standard attack set against one trajectory and summarise.
#
#   tools/run_experiments.sh data/placeholder.txt [max_frames]
#
# Each run boots Linux on the monitor, replays the commands in real time
# (20 Hz, so a 60 s flight takes 60 s), and scores the verdicts.
set -e
TRAJ="$1"
MAX="${2:-0}"
NAME=$(basename "$TRAJ" .txt)
mkdir -p data results

: > results/summary_$NAME.txt
for ATTACK in clean teleport overspeed fence replay flood mode drift; do
    OUT="data/${NAME}_${ATTACK}"
    python3 tools/inject.py "$TRAJ" --attack "$ATTACK" --out "$OUT" --max-frames "$MAX"
    make --no-print-directory replay CMD="$OUT.cmd" > /dev/null
    echo "=== $ATTACK ===" | tee -a "results/summary_$NAME.txt"
    python3 tools/score.py "results/${NAME}_${ATTACK}.log" "$OUT.labels.csv" | tee -a "results/summary_$NAME.txt"
    echo | tee -a "results/summary_$NAME.txt"
done
echo "summary in results/summary_$NAME.txt"
