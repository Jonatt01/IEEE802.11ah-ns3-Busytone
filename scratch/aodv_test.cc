/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 University of Washington
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */



/*
	LAB Assignment #4

	Solution by: Konstantinos Katsaros (K.Katsaros@surrey.ac.uk)
	based on wifi-simple-adhoc-grid.cc
*/

// The default layout is like this, on a 2-D grid.
//
// n20  n21  n22  n23  n24
// n15  n16  n17  n18  n19
// n10  n11  n12  n13  n14
// n5   n6   n7   n8   n9
// n0   n1   n2   n3   n4
//
// the layout is affected by the parameters given to GridPositionAllocator;
// by default, GridWidth is 5 and numNodes is 25..
//
// Flow 1: 0->24
// Flow 2: 20->4


#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/olsr-helper.h"
#include "ns3/aodv-module.h"
#include "ns3/dsdv-module.h"
#include "ns3/flow-monitor-module.h"
#include "myapp.h"
#include "ns3/random-variable-stream.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <numeric>
#include <random>

NS_LOG_COMPONENT_DEFINE ("Lab4");

using namespace ns3;



int main (int argc, char *argv[])
{
  // LogComponentEnableAll(LogLevel(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_PREFIX_NODE));
  // LogComponentEnable("AodvRoutingProtocol", LogLevel(LOG_LEVEL_ALL | LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_PREFIX_NODE));
  // LogComponentEnable("AodvRoutingTable", LogLevel(LOG_LEVEL_ALL | LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_PREFIX_NODE));
  

  std::string phyMode ("DsssRate1Mbps");
  double distance = 500;  //(m)
  uint32_t numNodes = 25;  // 5x5
  double interval = 0.01; // seconds(Default = 0.001)
  uint32_t packetSize = 500; // bytes(Default = 600)
  uint32_t numPackets = 1000;//1 vs 10000
  uint32_t numFlows = 2;  // must smaller than numNodes/2
  std::string rtslimit = "1500";  //(Default = 1000000)
  bool printRoutingTables = false;
  CommandLine cmd;

  cmd.AddValue ("phyMode", "Wifi Phy mode", phyMode);
  cmd.AddValue ("distance", "distance (m)", distance);
  cmd.AddValue ("packetSize", "distance (m)", packetSize);
  cmd.AddValue ("rtslimit", "RTS/CTS Threshold (bytes)", rtslimit);
  cmd.Parse (argc, argv);
  // Convert to time object
  Time interPacketInterval = Seconds (interval);

  // turn off RTS/CTS for frames below 2200 bytes
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue (rtslimit));
  // Fix non-unicast data rate to be the same as that of unicast
  Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue (phyMode));

  NodeContainer c;
  c.Create (numNodes);

  // The below set of helpers will help us to put together the wifi NICs we want
  WifiHelper wifi;

  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
  // set it to zero; otherwise, gain will be added
  wifiPhy.Set ("RxGain", DoubleValue (-10) ); 
  // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
  // wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO); 

  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel");
  wifiPhy.SetChannel (wifiChannel.Create ());

  // Add a non-QoS upper mac, and disable rate control
  NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
  wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",StringValue (phyMode),
                                "ControlMode",StringValue (phyMode));
  // Set it to adhoc mode
  wifiMac.SetType ("ns3::AdhocWifiMac");
  NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, c);

  MobilityHelper mobility;

  /*----------------------------------------------- */
  // random topology
  // set position allocator
  /*----------------------------------------------- */
  double min = 0.0;
  double max = 2000.0; // length of square side
  
  Ptr<UniformRandomVariable> uniform_rv = CreateObject<UniformRandomVariable>();
  uniform_rv->SetAttribute ("Min", DoubleValue (min));
  uniform_rv->SetAttribute ("Max", DoubleValue (max));

	Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
  for(int i=0; i < numNodes; ++i)
  {
    Ptr<MobilityModel> mob = c.Get(i)->GetObject<MobilityModel>();
    double x_pos = uniform_rv->GetValue (); // within the range [min, max)
    double y_pos = uniform_rv->GetValue (); // within the range [min, max)

		positionAlloc->Add(Vector(x_pos, y_pos, 0.0));
	}
	mobility.SetPositionAllocator(positionAlloc);
  /*----------------------------------------------- */

  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (c);

  // Enable Routing Protocol (AODV/DSDV)
  OlsrHelper olsr;
  AodvHelper aodv;
  DsdvHelper dsdv;

  Ipv4ListRoutingHelper list;
  list.Add (aodv, 10);//install Protocol to node

  InternetStackHelper internet;
  internet.SetRoutingHelper (list); // has effect on the next Install ()
  internet.Install (c);

  Ipv4AddressHelper ipv4;
  NS_LOG_INFO ("Assign IP Addresses.");
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");//給予ipv4address
  Ipv4InterfaceContainer ifcont = ipv4.Assign (devices);

  // print the specific node's routing table e.g., node 13
  if(printRoutingTables)
  {
    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper> (&std::cout);
    aodv.PrintRoutingTableAt (Seconds (1.0), c.Get(13), routingStream);
    aodv.PrintRoutingTableEvery(Seconds(10), c.Get(13), routingStream);
  }


  // Specify the path to the CSV file in the previous folder
  const std::string filePath = "../Python-Tools/node_position.csv";

  // Open the file for writing
  std::ofstream outputFile(filePath);

  // Check if the file is successfully opened
  if (!outputFile.is_open()) {
      std::cerr << "Error opening the file." << std::endl;
      return 1;
  }

  // Obtain the position of node i+1 (ns3 counts from zero)
  for(int i = 0; i < numNodes; ++i)
  {
    Ptr<MobilityModel> mob = c.Get(i)->GetObject<MobilityModel>();
    if(mob==0)
    {
      std::cout << "No Object of class MobilityModel in node " << i+1 << "." << std::endl;
      return 1;
    }
    double x = mob->GetPosition().x;
    double y = mob->GetPosition().y;
    double z = mob->GetPosition().z;

    // std::cout << "Position of node " << i+1 << " : " << "(" << x << ", " << y << ", " << z << " )" << std::endl;

    // Write numbers to the file
    outputFile << x << "," << y << std::endl;
  }
  
  outputFile.close();
  std::cout << "Position have been saved to: " << filePath << std::endl;

  // Create Apps
  uint16_t sinkPort = 6; // use the same for all apps

  // the vector for picking the transmitter and receiver
  unsigned seed = 1;
  std::vector<uint32_t> vec(numNodes); // generate vector of length numNodes
  std::iota(vec.begin(), vec.end(), 1); // fills the vector from 1 to numNodes
  std::shuffle(vec.begin(), vec.end(), std::default_random_engine(seed)); // shuffle the vector elements
  
  // Checking
  for(int loop = 0; loop < vec.size(); loop++)
  {
  std::cout << vec.at(loop) << '\t';

  if (0 == (loop + 1) % 10)
  {
    std::cout << '\n';
  }
  }
  std::cout << '\n';
  
  /*----------------------------------------------- */
  // Generate numFlow flows
  // pick nodes in order, according to the vec. 
  /*----------------------------------------------- */
  ApplicationContainer cbrApps; // The container for all applications

  for(int i = 0; i < numFlows ; ++i)
  {
    Address sinkAddress (InetSocketAddress (ifcont.GetAddress (vec[i*2]), sinkPort)); // interface of node vec[i]
    PacketSinkHelper packetSinkHelper1 ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
    ApplicationContainer sinkApps = packetSinkHelper1.Install (c.Get (vec[i*2])); // node vec[i] as sink
    sinkApps.Start (Seconds (0.0));
    sinkApps.Stop (Seconds (100.0));
    std::cout << vec[i*2+1] << " to " << vec[i*2] << std::endl;
    Ptr<Socket> ns3UdpSocket = Socket::CreateSocket (c.Get (vec[i*2+1]), UdpSocketFactory::GetTypeId ()); //source at n0

    // Create UDP application at n0
    Ptr<MyApp> app = CreateObject<MyApp> ();
    app->Setup (ns3UdpSocket, sinkAddress, packetSize, numPackets, DataRate ("1Mbps"));
    c.Get (vec[i*2+1])->AddApplication (app);
    app->SetStartTime (Seconds (31.0));
    app->SetStopTime (Seconds (100.0));
  }
  /*----------------------------------------------- */

  //flow 1
  // UDP connection from N0 to N24
  //  Address sinkAddress1 (InetSocketAddress (ifcont.GetAddress (vec[0]), sinkPort)); // interface of n24
  //  PacketSinkHelper packetSinkHelper1 ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  //  ApplicationContainer sinkApps1 = packetSinkHelper1.Install (c.Get (vec[0])); //n24 as sink
  //  sinkApps1.Start (Seconds (0.0));
  //  sinkApps1.Stop (Seconds (100.0));

  //  Ptr<Socket> ns3UdpSocket1 = Socket::CreateSocket (c.Get (vec[1]), UdpSocketFactory::GetTypeId ()); //source at n0

  //  // Create UDP application at n0
  //  Ptr<MyApp> app1 = CreateObject<MyApp> ();
  //  app1->Setup (ns3UdpSocket1, sinkAddress1, packetSize, numPackets, DataRate ("1Mbps"));
  //  c.Get (vec[1])->AddApplication (app1);
  //  app1->SetStartTime (Seconds (31.0));
  //  app1->SetStopTime (Seconds (100.0));


  // //flow 2
  // // UDP connection from N20 to N4
  // /*----------------------------------------------- */
  // //your code
  // /*----------------------------------------------- */
  //  Address sinkAddress2 (InetSocketAddress (ifcont.GetAddress (4), sinkPort)); // interface of n20
  //  PacketSinkHelper packetSinkHelper2 ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  //  ApplicationContainer sinkApps2 = packetSinkHelper2.Install (c.Get (4)); //n2 as sink
  //  sinkApps2.Start (Seconds (0.));
  //  sinkApps2.Stop (Seconds (100.));

  //  Ptr<Socket> ns3UdpSocket2 = Socket::CreateSocket (c.Get (20), UdpSocketFactory::GetTypeId ()); //source at n4

  // // Create UDP application at n20
  // /*----------------------------------------------- */
  // //your code
  // /*----------------------------------------------- */
  //  Ptr<MyApp> app2 = CreateObject<MyApp> ();
  //  app2->Setup (ns3UdpSocket2, sinkAddress2, packetSize, numPackets, DataRate ("1Mbps"));
  //  c.Get (20)->AddApplication (app2);
  //  app2->SetStartTime (Seconds (31.));
  //  app2->SetStopTime (Seconds (100.));
     

  // Install FlowMonitor on all nodes
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // wifiPhy.EnablePcap ("lab-4-solved", devices);
 
  Simulator::Stop (Seconds (100.0));
  Simulator::Run ();

  // Show Statistic Information
  double Avg_e2eDelaySec1 = 0.0;
  double Avg_SystemThroughput_bps1 = 0.0;
  double Avg_PDR1 = 0.0;
  double Avg_Packet_Loss_rate1 = 0.0;

  double Avg_e2eDelaySec2 = 0.0;
  double Avg_SystemThroughput_bps2 = 0.0;
  double Avg_PDR2 = 0.0;

  int tx_packets1 = 0;
  int rx_packets1 = 0;
  int tx_packets2 = 0;
  int rx_packets2 = 0;

  int Avg_Base1 = 0;
  int Avg_Base2 = 0;

  // Print per flow statistics
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats (); // access the private attribute m_flowStats in instance of class FlowMonitor

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter)
    {
	  Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);

      // Print per flow statistics
      /*implement flow monitor */
      /*----------------------------------------------- */
      //your code
      //需先判斷source address跟destination addresses是否為我們需要的
      //Ex:if(t.sourceAddress == Ipv4Address("??.?.?.?.?") && t.destinationAddress == Ipv4Address("??.?.?.??"))
      /*----------------------------------------------- */

    if (t.sourceAddress == Ipv4Address(ifcont.GetAddress(vec[1],0) ) && t.destinationAddress == Ipv4Address(ifcont.GetAddress(vec[0],0) ) ){
      // if( t.destinationPort == sinkPort ){
        std::cout << "hihi" << std::endl;
        Avg_e2eDelaySec1 += 
          (
            (double)iter -> second.delaySum.GetSeconds()/iter -> second.rxPackets
          );

        Avg_SystemThroughput_bps1 +=
          ( 
            (double)iter -> second.rxBytes*8/(iter -> second.timeLastRxPacket.GetSeconds() - iter -> second.timeFirstTxPacket.GetSeconds())
          );

        Avg_PDR1 = (double)iter -> second.rxPackets / iter -> second.txPackets;
        tx_packets1 = iter -> second.txPackets;
        rx_packets1 = iter -> second.rxPackets;
        Avg_Packet_Loss_rate1 = (double) (iter->second.txPackets - iter->second.rxPackets)*100 / (double) iter->second.txPackets;
        Avg_Base1 += 1;
      // }
    }
    else if (t.sourceAddress == Ipv4Address(ifcont.GetAddress(vec[3],0) ) && t.destinationAddress == Ipv4Address(ifcont.GetAddress(vec[2],0) ) ){
      // if( t.destinationPort == sinkPort ){
        Avg_e2eDelaySec2 += 
          (
            (double)iter -> second.delaySum.GetSeconds()/iter -> second.rxPackets
          );

        Avg_SystemThroughput_bps2 +=
          ( 
            (double)iter -> second.rxBytes*8/(iter -> second.timeLastRxPacket.GetSeconds() - iter -> second.timeFirstTxPacket.GetSeconds())
          );

        Avg_PDR2 = (double)iter -> second.rxPackets / iter -> second.txPackets;

        tx_packets2 = iter -> second.txPackets;
        rx_packets2 = iter -> second.rxPackets;
        Avg_Base2 += 1;
      // }
    }
  
  }

  std::cout << "---------------------------Flow 1 statistic result---------------------------" << std::endl;
  std::cout << "Flow ID 1 Src Addr " << Ipv4Address(ifcont.GetAddress(vec[0],0)) << " " << "Dest Addr " << Ipv4Address(ifcont.GetAddress(vec[1],0)) << std::endl;
  std::cout << "Tx packets : " << tx_packets1 << std::endl;
  std::cout << "Rx packets : " << rx_packets1 << std::endl;
  std::cout << "Avg_PDR : " << Avg_PDR1/Avg_Base1 << std::endl;  
  std::cout << "Avg_SystemThroughput : " << Avg_SystemThroughput_bps1/(Avg_Base1*1024) << " [Kbps]" << std::endl;
  std::cout << "Avg_e2eDelay : " << Avg_e2eDelaySec1/Avg_Base1 << " [Sec]" << std::endl;
  std::cout << "Packet loss ratio : " << Avg_Packet_Loss_rate1 << " % " << std::endl;

  // if (Avg_Base2 != 0){
    std::cout << "---------------------------Flow 2 statistic result---------------------------" << std::endl;
    std::cout << "Flow ID 1 Src Addr " << Ipv4Address(ifcont.GetAddress(vec[2],0)) << " " << "Dest Addr " << Ipv4Address(ifcont.GetAddress(vec[3],0)) << std::endl;
    std::cout << "Tx packets : " << tx_packets2 << std::endl;
    std::cout << "Rx packets : " << rx_packets2 << std::endl;
    std::cout << "Avg_PDR : " << Avg_PDR2/Avg_Base2 << std::endl;  
    std::cout << "Avg_SystemThroughput : " << Avg_SystemThroughput_bps2/(Avg_Base2*1024) << " [Kbps]" << std::endl;
    std::cout << "Avg_e2eDelay : " << Avg_e2eDelaySec2/Avg_Base2 << " [Sec]" << std::endl;  
  // }
  


  monitor->SerializeToXmlFile("lab-5.flowmon", true, true);

  Simulator::Destroy ();

  return 0;
}

