#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include "netif/etharp.h"
#include "ethernetif.h"
#include "netconf.h"
#include <stdio.h>
#include "LAN8742A.h"

struct netif gnetif;
static uint32_t LinkTimer = 0;

extern __IO uint32_t EthStatus;

static void tcpip_init_done_signal(void *arg)
{
  sys_sem_t *init_sem;

  init_sem = (sys_sem_t *)arg;
  sys_sem_signal(init_sem);
}

void LwIP_Init(void)
{
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;
  sys_sem_t init_sem;

#ifdef USE_DHCP
  ipaddr.addr = 0;
  netmask.addr = 0;
  gw.addr = 0;
#else
  IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
  IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
  IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
#endif

  if (sys_sem_new(&init_sem, 0) != ERR_OK) {
    printf("tcpip初始化信号量创建失败\r\n");
    return;
  }

  tcpip_init(tcpip_init_done_signal, &init_sem);
  sys_sem_wait(&init_sem);
  sys_sem_free(&init_sem);

  if (netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input) == NULL) {
    printf("netif_add失败\r\n");
    return;
  }

  netif_set_default(&gnetif);
  netif_set_link_callback(&gnetif, ETH_link_callback);

  if (EthStatus == (ETH_INIT_FLAG | ETH_LINK_FLAG)) {
    netif_set_link_up(&gnetif);
    netif_set_up(&gnetif);
    printf("IP: %d.%d.%d.%d\r\n", IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
    printf("子网掩码: %d.%d.%d.%d\r\n", NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
    printf("网关: %d.%d.%d.%d\r\n", GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
  } else {
    netif_set_link_down(&gnetif);
    netif_set_down(&gnetif);
    printf("以太网链路断开\r\n");
  }
}

void LwIP_Pkt_Handle(void)
{
  ethernetif_input(&gnetif);
}

void LwIP_Periodic_Handle(__IO uint32_t localtime)
{
  if ((localtime - LinkTimer) >= LINK_TIMER_INTERVAL) {
    LinkTimer = localtime;
    ETH_CheckLinkStatus(ETHERNET_PHY_ADDRESS);
  }
}
