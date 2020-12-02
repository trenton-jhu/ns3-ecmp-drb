if test "$#" -ne 3; then
    echo "Incorrect number of arguments"
    echo "Usage: ./run.sh [number of trails] [flow size] [load]"
    echo "Example: ./run.sh 5 1000000 0.1"
    exit 0
fi

rm -rf *.xml
echo "Begin to run $1 trails with parameters --runMode=ECMP --flowSize=$2 --load=$3"
for ((i = 1; i <= $1; i++)); do
    echo "Running trial $i ..."
    ./waf --run "ecmp-drb --ID=$i --runMode=ECMP --flowSize=$2 --load=$3" > /dev/null
done
echo "============ ECMP Results ============="
./parse.py *.xml
rm -rf *.xml

echo "Begin to run $1 trails with parameters --runMode=DRB --flowSize=$2 --load=$3"
for ((i = 1; i <= $1; i++)); do
    echo "Running trial $i ..."
    ./waf --run "ecmp-drb --ID=$i --runMode=DRB --flowSize=$2 --load=$3" > /dev/null
done
echo "============ DRB Results ============="
./parse.py *.xml
rm -rf *.xml

exit 0