#!/bin/sh
# Detectability bound: inject teleports of increasing size and record which are caught.
# The verifier should reject a step exactly when it exceeds d_min = V_MAX * 50 ms + EPS.
#
#   tools/sweep_teleport.sh data/placeholder.txt
set -e
TRAJ="$1"
NAME=$(basename "$TRAJ" .txt)
FRAMES="${2:-160}"
OUT="results/sweep_$NAME.txt"
mkdir -p data results
printf "%-14s %-10s %-10s %s\n" "teleport_mm" "step_mm" "result" "reason" > "$OUT"
for MM in 20 40 60 80 100 120 140 160 200 300 500; do
    M=$(python3 -c "print($MM/1000)")
    python3 tools/inject.py "$TRAJ" --attack teleport --magnitude "$M" --at 0.5 \
        --max-frames "$FRAMES" --out "data/${NAME}_tp$MM" > /dev/null
    make --no-print-directory replay CMD="data/${NAME}_tp$MM.cmd" > /dev/null
    LINE=$(python3 tools/score.py "results/${NAME}_tp$MM.log" "data/${NAME}_tp$MM.labels.csv")
    STEP=$(echo "$LINE" | sed -n 's/^step     : \([0-9]*\) mm.*/\1/p')
    RES=$(echo "$LINE"  | sed -n 's/^step     : .*(\(.*\))/\1/p')
    REASON=$(echo "$LINE" | sed -n 's/^latency  : first rejection at frame [0-9]* (\([A-Z]*\)).*/\1/p')
    printf "%-14s %-10s %-10s %s\n" "$MM" "$STEP" "$RES" "${REASON:--}" | tee -a "$OUT"
done
echo "d_min from policy.h: $(python3 -c "
import sys; sys.path.insert(0,'tools'); from traj import read_policy; p=read_policy()
print(p['V_MAX_MM_S']*0.05+p['EPS_MM'])") mm" | tee -a "$OUT"
