MAX_COUNT=$1
VAR=0
while [[ $VAR -lt $MAX_COUNT ]]
do
nice ./work 200 R 10000 &
VAR=`expr $VAR + 1`
done
