#!/bin/bash

FILE_PATH=$1
LIMIT=${2:-1000}
DATA_DIR="./data/"

if [ ! -f "$FILE_PATH" ]; then
    echo "Aborting: File '$FILE_PATH' does not exist." >&2
    exit 1
fi

{
    # Print the header first
    printf "%-15s | %-10s | %-6s | %-12s | %-8s | %-10s | %s\n" \
           "Event_Time" "Instrument" "Action" "Price" "Size" "Order_ID" "Role"
    echo "-------------------------------------------------------------------------------------------------"

    # Process the data, sort it, and remove the extra separators
    dbn "$FILE_PATH" --csv --limit "$LIMIT" | awk -F, '
    $6 == "T" || $6 == "F" {
        price = $8 / 1000000000;
        role = ($6 == "T") ? "AGGRESSOR TAPE" : "PASSIVE FILL";
        
        # Output raw format for sorting: TS_EVENT first
        printf "%s | %-10s | %-6s | %-12.2f | %-8s | %-10s | %s\n", 
               $2, $5, $6, price, $9, $11, role
    }' | sort -n | perl -MTime::Piece -ale '
        # Pretty-print timestamps
        if ($_ =~ /^(\d{10})(\d{9})/) {
            my $s = $1; my $ns = $2;
            my $time = Time::Piece->new($s)->strftime("%H:%M:%S");
            $_ =~ s/^\d{19}/$time.$ns/;
        }
        print $_;
    '
}
