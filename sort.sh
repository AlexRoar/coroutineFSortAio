if [ -z "$1" ]
  then
    echo "No files num supplied"
    return 1;
fi

ARGS=""
for ((i = 1 ; i <= $1 ; i++))
do
  ARGS="$ARGS data/$i.txt"
done
echo $ARGS
build/coSortAio $ARGS
