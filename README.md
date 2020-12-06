# NS3 Network Simulations on Fat-tree Topology
This implementation uses the [NS3](https://www.nsnam.org/) framework to run simulations for different variations of load-balancing techniques in datacenter networks. We support
ECMP (hash-based), Round-Robin, and Digit Reversal Bouncing ([DRB](http://conferences.sigcomm.org/co-next/2013/program/p49.pdf)) techniques. The simulation builds a fat-tree topology
and simulate several flows in the network and can output statistics like flow completion time (FCT) and throughput for comparison and analysis. 

## How to Run
Once you are inside the docker container, navigate to the `ns3-ecn-sharp` directory provided by doing:
```
cd ~/ns3-ecn-sharp
```
For the simplest way to run experiments, use the `run.sh` script. The usage for this script is as follow:
```
Usage: ./run.sh [number of trails] [flow size] [load]
```
For example, try doing:
```
./run.sh 3 1000000 0.1
```
This will run experiments and specify the flow size to be 1000000 (around 1 MB) and the network load to be 0.1 (the lowest network load). This will run 3 trials for each of the ECMP,
RR, and DRB techniques and output the average FCT and throughput over these trials and the standard deviation. You can try changing the parameters to run different experiments.


To run a specific experiment with more fine-tuned parameters, NS3 uses `./waf` to build and run. Our experiment module is called `ecmp-drb`, you can run the following to check out
the available parameters:
```
./waf --run "ecmp-drb --help"
```
You should see that the available program arguments listed as follow:
```
Program Arguments:
    --ID:                Simulation ID used to identify result output file [0]
    --K:                 Number of pods in fat-tree [4]
    --bwServerEdge:      Server to edge bandwidth (bps) [10000000000]
    --bwEdgeAgg:         Edge to aggregation bandwidth (bps) [10000000000]
    --bwAggCore:         Aggregation to core bandwidth (bps) [10000000000]
    --runMode:           Running mode of this simulation: ECMP, RR, or DRB [ECMP]
    --randomSeed:        Random seed, 0 for random generated [0]
    --flowSize:          Size of each flow [250]
    --load:              Load of the network, between 0.0 - 1.0 [0.1]
    --enableDcTcp:       Whether to enable DCTCP [true]
    --resequenceBuffer:  Whether to enable resequence buffer [false]
```
As an example, try running the following (this may up to several minutes, run with smaller load and flow size for faster experiments):
```
./waf --run "ecmp-drb --K=8 --runMode=DRB --load=0.5 --flowSize=500000"
```
Once this command complete, the result for this experiment will be written to an xml file. For the above parameters, the result would be saved in a file called `0-fattree-8-0.5-drb-500000.xml`
To actually aggregate and get the results we really want, we parse the xml file using the provided `parse.py` python script by running the following:
```
./parse.py 0-fattree-8-0.5-drb-500000.xml
```
This should print out the aggregate results including FCT, throughput and data for each flow tested to the screen.

## Acknowledgement
This simulation code depends on several modules implemented in [snowzjx/ns3-ecn-sharp](https://github.com/snowzjx/ns3-ecn-sharp) for their work on the following papers:
* [Enabling ECN for Datacenter Networks with RTT Variations (CoNEXT 19)](https://dl.acm.org/authorize.cfm?key=N690741)
* [Resilient Datacenter Load Balancing in the Wild (SIGCOMM 17)](http://www.cse.ust.hk/~kaichen/papers/hermes-sigcomm17.pdf)







