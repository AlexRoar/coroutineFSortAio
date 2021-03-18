if [ -z "$1" ]
  then
    echo "No files num supplied"
    return 1;
fi
sh generateTest.sh $1
sh sort.sh $1
sh checkTest.sh