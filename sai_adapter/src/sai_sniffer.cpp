#include "../inc/sai_adapter.h"

#define ETHER_ADDR_LEN 6
#define CPU_HDR_LEN 6
#define MAC_LEARN_TRAP_ID 512

typedef struct _ethernet_hdr_t {
  uint8_t dst_addr[ETHER_ADDR_LEN];
  uint8_t src_addr[ETHER_ADDR_LEN];
  uint16_t ether_type;
} ethernet_hdr_t;

typedef struct _cpu_hdr_t {
  unsigned int ingress_port : 8;
  unsigned int bridge_port : 8;
  unsigned int bridge_id : 16;
  unsigned int trap_id : 16;
} cpu_hdr_t;

void ReverseBytes(uint8_t *byte_arr, int size) {
  uint8_t tmp;
  for (int lo = 0, hi = size - 1; hi > lo; lo++, hi--) {
    tmp = byte_arr[lo];
    byte_arr[lo] = byte_arr[hi];
    byte_arr[hi] = tmp;
  }
}

void print_mac_to_log(const uint8_t *mac,std::shared_ptr<spdlog::logger> logger) {
  logger->info("{0:02X}:{1:02X}:{2:02X}:{3:02X}:{4:02X}:{5:02X}", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

void sai_adapter::PacketSniffer() {
  const char *dev = "cpu_port";

  char errbuf[PCAP_ERRBUF_SIZE];
  (*logger)->info("pcap started on dev {}", dev);
  adapter_pcap = pcap_open_live(dev, BUFSIZ, 0, -1, errbuf);
  if (adapter_pcap == NULL) {
    (*logger)->info("pcap_open_live() failed: {}", errbuf);
    return;
  }
  if (pcap_loop(adapter_pcap, 0, packetHandler, (u_char*)this) == -1) {
    (*logger)->info("pcap_loop() failed: {}", pcap_geterr(adapter_pcap));
  }
  return;
}

void sai_adapter::internal_init_switch() {
  (*logger)->info("Switch init with default configurations");
  sai_object_id_t *switch_id;
  switch_api.create_switch(switch_id, 0, NULL);
  return;
}

void sai_adapter::adapter_create_fdb_entry(sai_object_id_t bridge_port_id, sai_mac_t mac,
                              sai_fdb_entry_bridge_type_t bridge_type,
                              sai_vlan_id_t vlan_id,
                              sai_object_id_t bridge_id) {
  sai_attribute_t attr[3];
  attr[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
  attr[0].value.s32 = SAI_FDB_ENTRY_TYPE_STATIC;

  attr[1].id = SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID;
  attr[1].value.oid = bridge_port_id;

  attr[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;
  attr[2].value.s32 = SAI_PACKET_ACTION_FORWARD;

  sai_fdb_entry_t sai_fdb_entry;
  sai_fdb_entry.switch_id = 0;
  for (int i = 0; i < ETHER_ADDR_LEN; i++) {
    sai_fdb_entry.mac_address[i] = mac[i];
  }
  sai_fdb_entry.bridge_type = bridge_type;
  if (bridge_type == SAI_FDB_ENTRY_BRIDGE_TYPE_1Q) {
    sai_fdb_entry.vlan_id = (sai_vlan_id_t)vlan_id;
  }
  sai_fdb_entry.bridge_id = bridge_id;
  fdb_api.create_fdb_entry(&sai_fdb_entry, 3, attr);
}

void sai_adapter::packetHandler(u_char *userData,
                                const struct pcap_pkthdr *pkthdr,
                                const u_char *packet) {

  sai_adapter *adapter= (sai_adapter *) userData;
  (*logger)->info("CPU packet captured");
  cpu_hdr_t *cpu_hdr = (cpu_hdr_t *)packet;
  ReverseBytes((uint8_t *)cpu_hdr, CPU_HDR_LEN);
  ethernet_hdr_t *ether = (ethernet_hdr_t *)(packet + CPU_HDR_LEN);
  ReverseBytes((uint8_t *)&(ether->ether_type), 2);
  ReverseBytes(ether->dst_addr, 6);
  ReverseBytes(ether->src_addr, 6);
  if (cpu_hdr->trap_id == 512) {
    adapter->learn_mac(cpu_hdr->ingress_port, ether->src_addr);
  }
}

void sai_adapter::learn_mac(uint32_t ingress_port, uint8_t *src_mac) {
  // TODO: Add LAG support
  Port_obj *port;
  BridgePort_obj *bridge_port;
  Bridge_obj *bridge;
  for (port_id_map_t::iterator it = switch_metadata_ptr->ports.begin(); it != switch_metadata_ptr->ports.end(); ++ it) {
    if (it->second->hw_port == ingress_port) {
      port = it->second;
    }
  }
  for (bridge_port_id_map_t::iterator it = switch_metadata_ptr->bridge_ports.begin(); it != switch_metadata_ptr->bridge_ports.end(); ++ it) {
    if (it->second->port_id == port->sai_object_id) {
      bridge_port = it->second;
      bridge = switch_metadata_ptr->bridges[it->second->bridge_id];
    }
  }
  (*logger)->info("MAC learn:");
  print_mac_to_log(src_mac, *logger);
  sai_fdb_entry_bridge_type_t bridge_type;
  // adapter_create_fdb_entry(bridge_port_id, mac, bridge_type, vlan_id, bridge_id);
}