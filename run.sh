if test "$#" -ne 4; then
    echo "Incorrect number of arguments"
    echo "Usage: ./run.sh [number of trails] [ECMP/DRB] [flow size] [load]"
    echo "Example: ./run.sh 5 ECMP 1000000 0.1"
    exit 0
fi

rm -rf *.xml
echo "Begin to run $1 trails with parameters --runMode=$2 --flowSize=$3 --load=$4"
for ((i = 1; i <= $1; i++)); do
    echo "Running trial $i ..."
    ./waf --run "ecmp-drb --ID=$i --runMode=$2 --flowSize=$3 --load=$4" > /dev/null
done

./parse.py *.xml
rm -rf *.xml
exit 0