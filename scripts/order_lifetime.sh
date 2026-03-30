
FILE_NAME=$1
LIMIT=$2

DATA_DIR="./data/"

FILE_PATH="${DATA_DIR}$FILE_NAME"

if [ ! -f "$FILE_PATH" ]; then
    echo "Error: File $FILE_PATH was not found." >&2
    exit 1
fi

dbn $FILE_PATH --csv --limit $LIMIT | \
perl -MTime::Piece -F, -ale '
  # Store lines and count occurrences of each order_id
  push @{ $lines{$F[10]} }, $_ if $. > 1;
  $count{$F[10]}++;

  # When we hit the limit, print only IDs that appeared more than once
  END {
    print "ts_recv,ts_event,rtype,pub,inst,action,side,price,size,chan,order_id,flags,delta,seq";
    $printed = 0;
    foreach $id (sort keys %count) {
      if ($count{$id} > 1 && $id != 0) {
        foreach $l (@{ $lines{$id} }) {
          # Format the timestamp for readability
          @fields = split(",", $l);
          $s = $fields[0] / 1_000_000_000;
          $ns = $fields[0] % 1_000_000_000;
          $fields[0] = Time::Piece->new($s)->strftime("%H:%M:%S") . sprintf(".%09d", $ns);
          print join(",", @fields);
        }
        print "--------------------------------------------------------------------------------";
        $printed++;
        last if $printed >= 100;
      }
    }
  }
' | column -t -s,
