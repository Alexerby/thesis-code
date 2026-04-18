#!/usr/bin/env bash

FILE_PATH=$1
LIMIT=${2:-1000}
ACTION_FILTER=${3:-""}   # optional: pass "C" to filter by action
DATA_DIR="./data"

if [ -z "$FILE_NAME" ]; then
    echo "Usage: $0 <file> [limit] [action_filter]"
    echo "  e.g: $0 multi_instrument.dbn.zst 1000 C"
    exit 1
fi

if [ ! -f "$FILE_PATH" ]; then
    echo "File not found: $FILE_PATH"
    exit 1
fi

dbn "$FILE_PATH" --csv --limit "$LIMIT" | awk -F, -v action="$ACTION_FILTER" '
NR == 1 {
    printf "%-12s | %-12s | %-4s | %-4s | %-6s | %-4s | %-4s | %-20s | %-6s | %-4s | %-10s | %-5s | %-8s | %s\n",
        "T_Recv", "T_Event", "Rty", "Pub", "Inst", "Act", "Side", "Price", "Size", "Chan", "Order_ID", "Flg", "Delta", "Seq"
    print "-----------------------------------------------------------------------------------------------------------------------------------"
    next
}
action != "" && $6 != action { next }
{
    t_recv = strftime("%H:%M:%S", int($1 / 1000000000))
    t_evnt = strftime("%H:%M:%S", int($2 / 1000000000))
    printf "%-12s | %-12s | %-4s | %-4s | %-6s | %-4s | %-4s | %-20s | %-6s | %-4s | %-10s | %-5s | %-8s | %s\n",
        t_recv, t_evnt, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14
}
'
