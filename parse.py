#!/usr/bin/env python3
"""
Parser for output flow monitor xml files from running ns3 experiments
Modified from @snowzjx/ns3-ecn-sharp
"""
import argparse
from pathlib import Path
import statistics

try:
    from xml.etree import cElementTree as ElementTree
except ImportError:
    from xml.etree import ElementTree


def parse_time_ns(tm):
    if tm.endswith('ns'):
        return int(tm[:-4])
    raise ValueError(tm)


class FiveTuple(object):
    __slots__ = ['sourceAddress', 'destinationAddress', 'protocol', 'sourcePort', 'destinationPort']

    def __init__(self, el):
        self.sourceAddress = el.get('sourceAddress')
        self.destinationAddress = el.get('destinationAddress')
        self.sourcePort = int(el.get('sourcePort'))
        self.destinationPort = int(el.get('destinationPort'))
        self.protocol = int(el.get('protocol'))


class Histogram(object):
    __slots__ = 'bins', 'nbins', 'number_of_flows'

    def __init__(self, el=None):
        self.bins = []
        if el is not None:
            for bin in el.findall('bin'):
                self.bins.append((float(bin.get("start")), float(bin.get("width")), int(bin.get("count"))))


class Flow(object):
    __slots__ = ['flowId', 'delayMean', 'packetLossRatio', 'rxBitrate', 'txBitrate',
                 'fiveTuple', 'packetSizeMean', 'probe_stats_unsorted',
                 'hopCount', 'flowInterruptionsHistogram', 'rx_duration',
                 'fct', 'txBytes', 'txPackets', 'rxPackets', 'rxBytes', 'lostPackets', 'throughput']

    def __init__(self, flow_el):
        self.flowId = int(flow_el.get('flowId'))
        rxPackets = int(flow_el.get('rxPackets'))
        txPackets = int(flow_el.get('txPackets'))
        tx_duration = float(
            int(flow_el.get('timeLastTxPacket')[:-4]) - int(flow_el.get('timeFirstTxPacket')[:-4])) * 1e-9
        rx_duration = float(
            int(flow_el.get('timeLastRxPacket')[:-4]) - int(flow_el.get('timeFirstRxPacket')[:-4])) * 1e-9
        fct = float(int(flow_el.get('timeLastRxPacket')[:-4]) - int(flow_el.get('timeFirstTxPacket')[:-4])) * 1e-9
        txBytes = int(flow_el.get('txBytes'))
        rxBytes = int(flow_el.get('rxBytes'))
        self.txBytes = txBytes
        self.txPackets = txPackets
        self.rxBytes = rxBytes
        self.rxPackets = rxPackets
        self.rx_duration = rx_duration
        throughput = rxBytes * 8 / fct / 1024 / 1024
        if fct > 0:
            self.fct = fct
        else:
            self.fct = None
        if throughput > 0:
            self.throughput = throughput
        else:
            self.throughput = None
        self.probe_stats_unsorted = []
        if rxPackets:
            self.hopCount = float(flow_el.get('timesForwarded')) / rxPackets + 1
        else:
            self.hopCount = -1000
        if rxPackets:
            self.delayMean = float(flow_el.get('delaySum')[:-4]) / rxPackets * 1e-9
            self.packetSizeMean = float(flow_el.get('rxBytes')) / rxPackets
        else:
            self.delayMean = None
            self.packetSizeMean = None
        if rx_duration > 0:
            self.rxBitrate = int(flow_el.get('rxBytes')) * 8 / rx_duration
        else:
            self.rxBitrate = None
        if tx_duration > 0:
            self.txBitrate = int(flow_el.get('txBytes')) * 8 / tx_duration
        else:
            self.txBitrate = None
        lost = float(flow_el.get('lostPackets'))
        self.lostPackets = lost
        if rxPackets == 0:
            self.packetLossRatio = None
        else:
            self.packetLossRatio = (lost / (rxPackets + lost))

        interrupt_hist_elem = flow_el.find("flowInterruptionsHistogram")
        if interrupt_hist_elem is None:
            self.flowInterruptionsHistogram = None
        else:
            self.flowInterruptionsHistogram = Histogram(interrupt_hist_elem)


class ProbeFlowStats(object):
    __slots__ = ['probeId', 'packets', 'bytes', 'delayFromFirstProbe']


class Simulation(object):
    def __init__(self, simulation_el):
        self.flows = []
        FlowClassifier_el, = simulation_el.findall("Ipv4FlowClassifier")
        flow_map = {}
        for flow_el in simulation_el.findall("FlowStats/Flow"):
            flow = Flow(flow_el)
            flow_map[flow.flowId] = flow
            self.flows.append(flow)
        for flow_cls in FlowClassifier_el.findall("Flow"):
            flowId = int(flow_cls.get('flowId'))
            flow_map[flowId].fiveTuple = FiveTuple(flow_cls)

        for probe_elem in simulation_el.findall("FlowProbes/FlowProbe"):
            probeId = int(probe_elem.get('index'))
            for stats in probe_elem.findall("FlowStats"):
                flowId = int(stats.get('flowId'))
                s = ProbeFlowStats()
                s.packets = int(stats.get('packets'))
                s.bytes = int(stats.get('bytes'))
                s.probeId = probeId
                if s.packets > 0:
                    s.delayFromFirstProbe = parse_time_ns(stats.get('delayFromFirstProbeSum')) / float(s.packets)
                else:
                    s.delayFromFirstProbe = 0
                flow_map[flowId].probe_stats_unsorted.append(s)


def parse(fileName, print_flows=False):
    file_obj = open(str(fileName), "r")
    print("Reading XML file: " + str(fileName))

    level = 0
    sim_list = []
    for event, elem in ElementTree.iterparse(file_obj, events=("start", "end")):
        if event == "start":
            level += 1
        if event == "end":
            level -= 1
            if level == 0 and elem.tag == 'FlowMonitor':
                sim = Simulation(elem)
                sim_list.append(sim)
                elem.clear()

    total_fct = 0
    total_throughput = 0
    flow_count = 0
    total_lost_packets = 0
    total_packets = 0
    total_rx_packets = 0

    flow_list = []

    for sim in sim_list:
        for flow in sim.flows:
            if flow.fct is None or flow.txBitrate is None or flow.rxBitrate is None or flow.throughput is None:
                continue
            if 52 * flow.txPackets + 4 <= flow.txBytes <= 52 * flow.txPackets + 4 * 6:
                continue
            flow_count += 1
            total_fct += flow.fct
            total_packets += flow.txPackets
            total_rx_packets += flow.rxPackets
            total_lost_packets += flow.lostPackets
            total_throughput += flow.throughput
            flow_list.append(flow)
            t = flow.fiveTuple
            proto = {6: 'TCP', 17: 'UDP'}[t.protocol]
            if print_flows:
                print("FlowID: %i (%s %s/%s --> %s/%i)" % (
                    flow.flowId, proto, t.sourceAddress, t.sourcePort, t.destinationAddress, t.destinationPort))
                print("\tTX bitrate: %.2f kbit/s" % (flow.txBitrate * 1e-3,))
                print("\tRX bitrate: %.2f kbit/s" % (flow.rxBitrate * 1e-3,))
                print("\tMean Delay: %.2f ms" % (flow.delayMean * 1e3,))
                print("\tFlow size: %i bytes, %i packets" % (flow.txBytes, flow.txPackets))
                print("\tRx %i bytes, %i packets" % (flow.rxBytes, flow.rxPackets))
                print("\tThroughput: %.4f" % flow.throughput)
                print("\tFCT: %.4f" % flow.fct)

    avg_fct = total_fct / flow_count
    avg_tp = total_throughput / flow_count
    print("Avg FCT: %.6f" % avg_fct)
    print("Avg throughput: %.6f" % avg_tp)
    print("Total TX Packets: %i" % total_packets)
    print("Total RX Packets: %i" % total_rx_packets)

    flow_list.sort(key=lambda x: x.fct)
    index_99 = int(len(flow_list) * 0.99)
    flow_fct_99 = flow_list[index_99].fct

    print("The FCT of 99 flow is: %.6f" % flow_fct_99)
    print("===========================================")
    return avg_fct, avg_tp


def parse_args():
    parser = argparse.ArgumentParser(description="Process flow monitor xml files")
    parser.add_argument("files", type=Path, help="Flow monitor xml files to parse", nargs='+')
    args = parser.parse_args()
    return args


def main():
    args = parse_args()
    num = len(args.files)
    if num == 1:
        parse(args.files[0], True)
    elif num > 1:
        fcts = []
        tps = []
        for fileName in args.files:
            fct, tp = parse(fileName)
            fcts.append(fct)
            tps.append(tp)

        print("Aggregated results over %i trials:" % num)
        print("FCT Avg: %.6f" % statistics.mean(fcts))
        print("FCT Stdev: %.6f" % statistics.stdev(fcts))
        print("Throughput Avg: %.6f" % statistics.mean(tps))
        print("Throughput Stdev: %.6f" % statistics.stdev(tps))


if __name__ == '__main__':
    main()
