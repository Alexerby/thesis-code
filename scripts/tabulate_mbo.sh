FILE_PATH=$1
LIMIT=${2:-1000}
DATA_DIR="./data"

if [ ! -f "$FILE_PATH" ]; then
    echo "File path: $FILE_PATH not found"
    exit 1
fi

{
    dbn "$FILE_PATH" --csv --limit "$LIMIT" | awk -F, '
    NR == 1 {
        # Define Header
        printf "%-10s | %-10s | %-4s | %-4s | %-6s | %-4s | %-4s | %-20s | %-6s | %-4s | %-10s | %-5s | %-8s | %s\n", 
        "T_Recv", "T_Event", "Rty", "Pub", "Inst", "Act", "Side", "Price", "Size", "Chan", "Order_ID", "Flg", "Delta", "Seq"
        print "------------------------------------------------------------------------------------------------------------------------------------------------------"
        next
    }
    {
        # Convert both timestamps to readable H:M:S format
        t_recv = strftime("%H:%M:%S", $1 / 1000000000)
        t_evnt = strftime("%H:%M:%S", $2 / 1000000000)
        
        # Mapping all 14 columns
        printf "%-10s | %-10s | %-4s | %-4s | %-6s | %-4s | %-4s | %-20s | %-6s | %-4s | %-10s | %-5s | %-8s | %s\n", 
        t_recv, t_evnt, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14
    }
    '
}
