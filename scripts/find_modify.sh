#!/bin/bash

FILE_PATH=$1
LIMIT=${2:-10000000}
DATA_DIR="./data/"

echo "Hunting for the elusive Action::M (Modify) in $LIMIT records..."

dbn "$FILE_PATH" --csv --limit "$LIMIT" | awk -F, '
BEGIN {
    count = 0
    printf "%-15s | %-10s | %-8s | %-12s | %-8s | %-10s\n", 
           "Event_Time", "Order_ID", "Action", "Price", "Size", "Seq"
    print "--------------------------------------------------------------------------------"
}
# Filter for Action M (6th column)
$6 == "M" {
    count++
    price = $8 / 1e9
    printf "%-15s | %-10s | %-8s | %-12.2f | %-8s | %-10s\n", 
           $2, $11, $6, price, $9, $14
    if (count >= 10) exit
}
END {
    if (count == 0) print ">>> NO MODIFY ACTIONS FOUND IN THE SAMPLE <<<"
    else print ">>> FOUND " count " MODIFY EVENTS <<<"
}'
