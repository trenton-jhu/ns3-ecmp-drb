/**
 * Modified fat-tree simulation code from @snowzjx/ns3-ecn-sharp
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-xpath-routing-helper.h"
#include "ns3/ipv4-tlb.h"
#include "ns3/ipv4-tlb-probing.h"
#include "ns3/ipv4-drb-routing-helper.h"

#include <map>
#include <utility>

#define LINK_CAPACITY_BASE 1000000000
#define LINK_DELAY  MicroSeconds(10)
#define BUFFER_SIZE 600                     
#define PACKET_SIZE 1400
#define FLOW_DIST_FACTOR 12658200

#define RED_QUEUE_MARKING 65

#define PORT_START 10000
#define PORT_END 50000

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ECMP-DRB-Simulation");

enum RunMode {
    ECMP,
    RR,
    DRB,
};

/**
 * Generate random time interval based on poisson distribution
 */
double poission_gen_interval(double avg_rate)
{
    if (avg_rate > 0)
       return -logf(1.0 - (double)rand() / RAND_MAX) / avg_rate;
    else
       return 0;
}

template<typename T>
T rand_range (T min, T max)
{
    return min + ((double)max - min) * rand () / RAND_MAX;
}

/**
 * Install simulation based on passed in parameters
 */
void install_applications (uint32_t fromPodId, uint32_t serverCount, uint32_t k, NodeContainer servers,
                           double requestRate, uint32_t flow_size_override,
                           double START_TIME, double END_TIME, double FLOW_LAUNCH_END_TIME)
{
    for (uint32_t i = 0; i < serverCount * (k / 2); i++)
    {
        uint32_t fromServerIndex = fromPodId * serverCount * (k / 2) + i;

        double startTime = START_TIME + poission_gen_interval (requestRate);
        while (startTime < FLOW_LAUNCH_END_TIME)
        {
            uint16_t port = rand_range (PORT_START, PORT_END);

            uint32_t destServerIndex = fromServerIndex;
            while (destServerIndex >= fromPodId * serverCount * (k / 2)
                    && destServerIndex < (fromPodId + 1) * serverCount * (k / 2))
            {
                destServerIndex = rand_range (0u, serverCount * (k / 2) * k);
            }

	        Ptr<Node> destServer = servers.Get (destServerIndex);
	        Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
	        Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1, 0);
	        Ipv4Address destAddress = destInterface.GetLocal ();

            BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (destAddress, port));
            uint32_t flowSize = flow_size_override;

 	        source.SetAttribute ("SendSize", UintegerValue (PACKET_SIZE));
            source.SetAttribute ("MaxBytes", UintegerValue(flowSize));

            ApplicationContainer sourceApp = source.Install (servers.Get (fromServerIndex));
            sourceApp.Start (Seconds (startTime));
            sourceApp.Stop (Seconds (END_TIME));

            PacketSinkHelper sink ("ns3::TcpSocketFactory",
                    InetSocketAddress (Ipv4Address::GetAny (), port));
            ApplicationContainer sinkApp = sink.Install (servers. Get (destServerIndex));
            sinkApp.Start (Seconds (startTime));
            sinkApp.Stop (Seconds (END_TIME));

            startTime += poission_gen_interval (requestRate);
        }
    }
}

/**
 * Return formatted output flow monitor xml file name
 */
std::string get_output_filename(int id, RunMode runMode, double load, uint32_t k, uint32_t flow_size)
{
    std::stringstream filename;
    filename << id << "-" << "fattree-" << k << "-" << load << "-";
    if (runMode == ECMP)
        filename << "ecmp-";
    else if (runMode == DRB)
        filename << "drb-";
    else if (runMode == RR) 
        filename << "rr-";
    
    filename << flow_size << ".xml";
    return filename.str();
}

/**
 * Main entry point of simulation
 * called using
 * ./waf --run "ecmp-drb --help"
 */
int main (int argc, char *argv[])
{

#if 1
    LogComponentEnable ("ECMP-DRB-Simulation", LOG_LEVEL_INFO);
#endif

    std::string runModeStr = "ECMP";
    unsigned randomSeed = 0;
    double load = 0.1;

    double START_TIME = 0.0;
    double END_TIME = 0.5;
    double FLOW_LAUNCH_END_TIME = 0.2;

    uint32_t k = 4; // size of fat-tree (number of pods)

    bool dctcpEnabled = true;
    bool resequenceBuffer = false;

    int simulation_id = 0;
    uint32_t flow_size_override = 250; // Default flow size

    uint64_t serverEdgeCapacity = 10ul * LINK_CAPACITY_BASE;    // Default 10 Gbps
    uint64_t edgeAggregationCapacity = 10ul * LINK_CAPACITY_BASE;
    uint64_t aggregationCoreCapacity = 10ul * LINK_CAPACITY_BASE;

    CommandLine cmd;
    cmd.AddValue ("ID", "Simulation ID used to identify result output file", simulation_id);
    cmd.AddValue ("K", "Number of pods in fat-tree", k);
    cmd.AddValue ("bwServerEdge", "Server to edge bandwidth (bps)", serverEdgeCapacity);
    cmd.AddValue ("bwEdgeAgg", "Edge to aggregation bandwidth (bps)", edgeAggregationCapacity);
    cmd.AddValue ("bwAggCore", "Aggregation to core bandwidth (bps)", aggregationCoreCapacity);
    cmd.AddValue ("runMode", "Running mode of this simulation: ECMP, RR, or DRB", runModeStr);
    cmd.AddValue ("randomSeed", "Random seed, 0 for random generated", randomSeed);
    cmd.AddValue ("flowSize", "Size of each flow", flow_size_override);
    cmd.AddValue ("load", "Load of the network, between 0.0 - 1.0", load);
    cmd.AddValue ("enableDcTcp", "Whether to enable DCTCP", dctcpEnabled);
    cmd.AddValue ("resequenceBuffer", "Whether to enable resequence buffer", resequenceBuffer);

    cmd.Parse (argc, argv);

    RunMode runMode;
    if (runModeStr.compare ("ECMP") == 0)
    {
        runMode = ECMP;
    }
    else if (runModeStr.compare ("DRB") == 0)
    {
        runMode = DRB;
    }
    else if (runModeStr.compare("RR") == 0)
    {
        runMode = RR;
    }
    else
    {
        NS_LOG_ERROR ("Run mode must be either ECMP, RR, or DRB");
        return 0;
    }

    if (load <= 0.0 || load >= 1.0)
    {
        NS_LOG_ERROR ("The network load must be within 0.0 and 1.0");
        return 0;
    }

    if (dctcpEnabled)
    {
        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpDCTCP::GetTypeId ()));
        Config::SetDefault ("ns3::RedQueueDisc::Mode", StringValue ("QUEUE_MODE_BYTES"));
    	Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (PACKET_SIZE));
        Config::SetDefault ("ns3::RedQueueDisc::QueueLimit", UintegerValue (BUFFER_SIZE * PACKET_SIZE));
        Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (false));
    }

    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue(PACKET_SIZE));
    Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
    Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (5)));
    Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
    Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (5)));
    Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (MicroSeconds (100)));
    Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (80)));
    Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (160000000));
    Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (160000000));


    Config::SetDefault ("ns3::RedQueueDisc::Mode", StringValue ("QUEUE_MODE_BYTES"));
    Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (PACKET_SIZE));
    Config::SetDefault ("ns3::RedQueueDisc::QueueLimit", UintegerValue (BUFFER_SIZE * PACKET_SIZE));
    Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (false));
    Config::SetDefault ("ns3::Ipv4GlobalRouting::PerflowEcmpRouting", BooleanValue (true));

    if (resequenceBuffer)
    {
	    Config::SetDefault ("ns3::TcpSocketBase::ResequenceBuffer", BooleanValue (true));
        Config::SetDefault ("ns3::TcpResequenceBuffer::InOrderQueueTimerLimit", TimeValue (MicroSeconds (15)));
        Config::SetDefault ("ns3::TcpResequenceBuffer::SizeLimit", UintegerValue (100));
        Config::SetDefault ("ns3::TcpResequenceBuffer::OutOrderQueueTimerLimit", TimeValue (MicroSeconds (250)));
    }

    if (k % 2 != 0)
    {
        NS_LOG_ERROR ("Invalid value for K");
        return 0;
    }

    uint32_t serverCount = k / 2;

    uint32_t edgeCount = k * (k / 2);
    uint32_t aggregationCount = k * (k / 2);
    uint32_t coreCount = (k / 2) * (k / 2);

    NodeContainer servers;
    NodeContainer edges;
    NodeContainer aggregations;
    NodeContainer cores;

    servers.Create (serverCount * edgeCount);
    edges.Create (edgeCount);
    aggregations.Create (aggregationCount);
    cores.Create (coreCount);

    InternetStackHelper internet;
    Ipv4GlobalRoutingHelper globalRoutingHelper;
    Ipv4ListRoutingHelper listRoutingHelper;
    Ipv4XPathRoutingHelper xpathRoutingHelper;
    Ipv4DrbRoutingHelper drbRoutingHelper;

    if (runMode == DRB || runMode == RR)
    {
        Config::SetDefault ("ns3::Ipv4DrbRouting::Mode", UintegerValue (0));

        listRoutingHelper.Add (drbRoutingHelper, 1);
        listRoutingHelper.Add (globalRoutingHelper, 0);
        internet.SetRoutingHelper (listRoutingHelper);
        internet.Install (servers);

        listRoutingHelper.Clear ();
        listRoutingHelper.Add (xpathRoutingHelper, 1);
        listRoutingHelper.Add (globalRoutingHelper, 0);
        internet.SetRoutingHelper (listRoutingHelper);
        internet.Install (edges);
        internet.Install (aggregations);
        internet.Install (cores);
    }
    else if (runMode == ECMP)
    {
	    internet.SetRoutingHelper (globalRoutingHelper);

	    internet.Install (servers);
	    internet.Install (edges);
        internet.Install (aggregations);
        internet.Install (cores);
    }

    PointToPointHelper p2p;

    if (dctcpEnabled)
        p2p.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (10));
    else
        p2p.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (BUFFER_SIZE));

    Ipv4AddressHelper ipv4;

    ipv4.SetBase ("10.1.0.0", "255.255.255.0");

    TrafficControlHelper tc;

    if (dctcpEnabled)
    {
        tc.SetRootQueueDisc ("ns3::RedQueueDisc", "MinTh", DoubleValue (RED_QUEUE_MARKING * PACKET_SIZE),
                                                  "MaxTh", DoubleValue (RED_QUEUE_MARKING * PACKET_SIZE));
    }

    p2p.SetChannelAttribute ("Delay", TimeValue (LINK_DELAY));

    std::map<std::pair<int, int>, uint32_t> edgeToAggregationPath;
    std::map<std::pair<int, int>, uint32_t> aggregationToCorePath;

    NS_LOG_INFO ("Creating fat-tree topology");
    // Hosts to Edge Switches
    p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (serverEdgeCapacity)));
    for (uint32_t i = 0; i < edgeCount; i++)
    {
        ipv4.NewNetwork ();
        for (uint32_t j = 0; j < serverCount; j++)
        {
            uint32_t uServerIndex = i * serverCount + j;

            NodeContainer nodeContainer = NodeContainer (edges.Get (i), servers.Get (uServerIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

            if (dctcpEnabled)
                tc.Install (netDeviceContainer);

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

            if (!dctcpEnabled)
                tc.Uninstall (netDeviceContainer);

            NS_LOG_INFO ("Server-" << uServerIndex << " is connected to Edge-" << i
                    << " (" << netDeviceContainer.Get (1)->GetIfIndex () << "<->"
                    << netDeviceContainer.Get (0)->GetIfIndex () << ")");
        }
    }

    // Edge to Aggregation Switches
    p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (edgeAggregationCapacity)));
    for (uint32_t i = 0; i < edgeCount; i++)
    {
        for (uint32_t j = 0; j < k / 2; j++)
        {
            uint32_t uAggregationIndex = (i / (k / 2)) * (k / 2) + j;

            NodeContainer nodeContainer = NodeContainer (edges.Get (i), aggregations.Get (uAggregationIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

            if (dctcpEnabled)
                tc.Install (netDeviceContainer);

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

            if (!dctcpEnabled)
                tc.Uninstall (netDeviceContainer);

            std::pair<uint32_t, uint32_t> pathKey = std::make_pair (i, uAggregationIndex);
            edgeToAggregationPath[pathKey] = netDeviceContainer.Get (0)->GetIfIndex ();

            NS_LOG_INFO ("Edge-" << i << " is connected to Aggregation-" << uAggregationIndex
                    << " (" << netDeviceContainer.Get (0)->GetIfIndex () << "<->"
                    << netDeviceContainer.Get (1)->GetIfIndex () << ")");
        }
    }

    // Aggregation to Core Switches
    p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (aggregationCoreCapacity)));
    for (uint32_t i = 0; i < aggregationCount; i++)
    {
        for (uint32_t j = 0; j < k /2; j++)
        {
            uint32_t uCoreIndex = (i % (k / 2)) * (k / 2) + j;

            NodeContainer nodeContainer = NodeContainer (aggregations.Get (i), cores.Get (uCoreIndex));
            NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

            if (dctcpEnabled)
                tc.Install (netDeviceContainer);

            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

            if (!dctcpEnabled)
                tc.Uninstall (netDeviceContainer);

            std::pair<uint32_t, uint32_t> pathKey = std::make_pair (i, uCoreIndex);
            aggregationToCorePath[pathKey] = netDeviceContainer.Get (0)->GetIfIndex ();

            NS_LOG_INFO ("Aggregation-" << i << " is connected to Core-" << uCoreIndex
                    << " (" << netDeviceContainer.Get (0)->GetIfIndex () << "<->"
                    << netDeviceContainer.Get (1)->GetIfIndex () << ")");
        }
    }

    // Install DRB and RR
    // drbRoutingHelper simply uses paths given in order, so we change the order for DRB
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    if (runMode == DRB)
    {
        for (uint32_t i = 0; i < edgeCount; i++)
        {
            for (uint32_t j = 0; j < serverCount; j++)
            {
                uint32_t uServerIndex = i * serverCount + j;
                for (uint32_t n = 0; n < k / 2; n++)
                {
                    for (uint32_t m = 0; m < k /2; m++)
                    {
                        uint32_t uCoreIndex = m * (k / 2) + n;
                        uint32_t uAggregationIndex = (i / (k / 2)) * (k / 2) + m;

                        int path = 0;
                        int pathBase = 1;
                        path += edgeToAggregationPath[std::make_pair (i, uAggregationIndex)] * pathBase;
                        pathBase *= 100;
                        int newPath = aggregationToCorePath[std::make_pair (uAggregationIndex, uCoreIndex)] * pathBase + path;
                        Ptr<Ipv4DrbRouting> drbRouting = drbRoutingHelper.GetDrbRouting (servers.Get (uServerIndex)->GetObject<Ipv4> ());
                        drbRouting->AddPath (newPath);

                    }
                }
            }
        }
    }
    if (runMode == RR) {
        for (uint32_t i = 0; i < edgeCount; i++)
        {
            for (uint32_t j = 0; j < serverCount; j++)
            {
                uint32_t uServerIndex = i * serverCount + j;
                for (uint32_t m = 0; m < k /2; m++)
                {
                    uint32_t uAggregationIndex = (i / (k / 2)) * (k / 2) + m;
                    int path = 0;
                    int pathBase = 1;
                    path += edgeToAggregationPath[std::make_pair (i, uAggregationIndex)] * pathBase;
                    pathBase *= 100;
                    for (uint32_t n = 0; n < k / 2; n++)
                    {
                        uint32_t uCoreIndex = m * (k / 2) + n;
                        int newPath = aggregationToCorePath[std::make_pair (uAggregationIndex, uCoreIndex)] * pathBase + path;
                        Ptr<Ipv4DrbRouting> drbRouting = drbRoutingHelper.GetDrbRouting (servers.Get (uServerIndex)->GetObject<Ipv4> ());
                        drbRouting->AddPath (newPath);
                    }
                }
            }

        }
    }

    double oversubRatio = static_cast<double> (serverCount * (k / 2) * k * serverEdgeCapacity) / (aggregationCoreCapacity * (k / 2) * aggregationCount);

    double requestRate = load * serverEdgeCapacity / oversubRatio / (8 * FLOW_DIST_FACTOR);

    if (randomSeed == 0)
        srand ((unsigned) time (NULL));
    else
        srand (randomSeed);

    for (uint32_t podId = 0; podId < k; podId++)
    {
        install_applications (podId, serverCount, k, servers, requestRate, flow_size_override, START_TIME, END_TIME, FLOW_LAUNCH_END_TIME);
    }

    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    NS_LOG_INFO ("Start simulation");
    Simulator::Stop (Seconds (END_TIME));
    Simulator::Run ();

    std::string output_filename = get_output_filename(simulation_id, runMode, load, k, flow_size_override);
    flowMonitor->SerializeToXmlFile(output_filename, true, true);

    Simulator::Destroy ();
    NS_LOG_INFO ("Stop simulation");

    return 0;
}

