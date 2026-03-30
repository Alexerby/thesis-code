FILE_NAME=$1
LIMIT=${2:-1000}
DATA_DIR="./data"
FILE_PATH="${DATA_DIR}/$FILE_NAME"

if [ ! -f "$FILE_PATH" ]; then
    echo "File path: $FILE_PATH not found"
    exit 1
fi

{
    echo -e "\n"
    dbn "$FILE_PATH" --csv --limit "$LIMIT" | awk -F, '
    NR == 1 {
        printf "%-20s | %-15s | %-6s | %-10s | %-8s | %-10s | %s\n", 
        "Timestamp", $5, $6, $7, $9, $11, $12
        print "------------------------------------------------------------------------------------------------"
        next
    }
    {
        # Convert nanoseconds to seconds for strftime
        # Use $2 if ts_event is the second column
        readable_ts = strftime("%H:%M:%S", $2 / 1000000000)
        
        printf "%-20s | %-15s | %-6s | %-10s | %-8s | %-10s | %s\n", 
        readable_ts, $5, $6, $7, $9, $11, $12
    }
    '
}
