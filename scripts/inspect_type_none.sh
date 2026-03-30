#!/bin/bash

FILE_NAME=$1
LIMIT=${2:-10000000} # Scan 10M rows by default
DATA_DIR="./data/"
FILE_PATH="${DATA_DIR}$FILE_NAME"

echo "Scanning $LIMIT records for Action::N (None)..."

dbn "$FILE_PATH" --csv --limit "$LIMIT" | awk -F, '
BEGIN {
    count = 0
    printf "%-25s | %-5s | %-10s | %-6s | %-10s | %s\n", 
           "ts_event", "inst", "action", "flags", "sequence", "context"
    print "------------------------------------------------------------------------------------------------"
}

# Filter for Action N (6th column)
$6 == "N" {
    count++
    
    # Context Logic: Decode common Databento flags
    ctx = "Heartbeat/Other"
    if ($12 == 1) ctx = "Snapshot Start"
    if ($12 == 2) ctx = "Snapshot End"
    if ($12 == 128) ctx = "Last in Message"

    printf "%-25s | %-5s | %-10s | %-6s | %-10s | %s\n", 
           $2, $5, $6, $12, $14, ctx
    
    if (count >= 20) exit
}

END {
    if (count == 0) print "\n>>> NO ACTION::N MESSAGES FOUND IN THIS RANGE <<<"
    else print "\n>>> TOTAL N MESSAGES FOUND: " count " <<<"
}'
