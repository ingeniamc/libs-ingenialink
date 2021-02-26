/**
 * @file lwip.c
 *
 * @author  Firmware department.
 * @copyright Ingenia Motion Control (c) 2019. All rights reserved.
 */

#include "lwip.h"

#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"

#include <stdint.h>

/* Default Ip address */
#define DEFAULT_IP_ADDR1        (uint8_t)192
#define DEFAULT_IP_ADDR2        (uint8_t)168
#define DEFAULT_IP_ADDR3        (uint8_t)1
#define DEFAULT_IP_ADDR4        (uint8_t)1

/* Default gateway address */
#define DEFAULT_GW_IP_ADDR1     (uint8_t)192
#define DEFAULT_GW_IP_ADDR2     (uint8_t)168
#define DEFAULT_GW_IP_ADDR3     (uint8_t)1
#define DEFAULT_GW_IP_ADDR4     (uint8_t)1

/* Default Submask */
#define DEFAULT_MASK_ADDR1      (uint8_t)255
#define DEFAULT_MASK_ADDR2      (uint8_t)255
#define DEFAULT_MASK_ADDR3      (uint8_t)255
#define DEFAULT_MASK_ADDR4      (uint8_t)0

/* Default Udp open port */
#define UDP_OPEN_PORT           (uint16_t)1061U

/* Network instance */
struct netif tNetif;

/* Global reply data buffer */
uint8_t pReplyData[1024U];

/**
 * Initialization of network instance.
 *
 * @note This function should be passed as a parameter to netif_add(),
 *        to be linked as initialization callback.
 *
 * @param[in] ptNetIfHnd
 *  Lwip network interface structure for this ethernet.
 *
 * @retval ERR_OK if the instance initialized successfully,
 *          error code otherwise.
 */
static err_t
LWIP_EthernetifInit(struct netif *ptNetIfHnd);

/**
 * Write data to network instance (EoE).
 *
 * @note This function has to be linked as network output callback
 *
 * @param[in] ptNetIfHnd
 *  Unused
 * @param[in] ptBuf
 *  Pointer to data buffer
 *
 * @retval ERR_OK if all success, error code otherwise
 */
static err_t
LWIP_EthernetifOutput(struct netif *ptNetIfHnd, struct pbuf *ptBuf);

/**
 * Process Udp received data.
 *
 * @note Blocking function until reply received.
 *
 * @param[in] pArg
 *  Unused (supplied argument)
 * @param[in] ptUdpPcb
 *  Pointer to udp instance
 * @param[in] ptBuf
 *  Pointer to packet buffer received
 * @param[in] ptAddr
 *  Pointer to remote Ip address from which the packet was received
 * @param[in] u16Port
 *  Remote port from which the packet was received
 */
static void LWIP_UdpReceiveData(void* pArg, struct udp_pcb* ptUdpPcb, struct pbuf* ptBuf,
                                const ip_addr_t* ptAddr, u16_t u16Port)
{
    printf("UDP received!!");
    // uint16_t u16SzBy;
    // struct pbuf* pRepBuf = NULL;

    // // CocoApp_MsgProces(ptBuf->payload, &(ptBuf->len), pReplyData, &u16SzBy);

    // /* Create response and copy the data */
    // pRepBuf = pbuf_alloc(PBUF_TRANSPORT, u16SzBy, PBUF_RAM);
    // memcpy((void*)pRepBuf->payload, (const void*)pReplyData, u16SzBy);

    // /* Send out the response */
    // udp_sendto(ptUdpPcb, pRepBuf, ptAddr, u16Port);

    // if (ptBuf != NULL)
    // {
    //     pbuf_free(ptBuf);
    //     ptBuf = NULL;
    // }
    // if (pRepBuf != NULL)
    // {
    //     pbuf_free(pRepBuf);
    //     pRepBuf = NULL;
    // }
}

void LWIP_Init(void)
{
    ip4_addr_t tIpAddr, tNetmask, tGwIpAddr;
    struct udp_pcb *ptUdpPcb;

    /* Initilialize the LwIP stack without RTOS */
    lwip_init();

    /* IP addresses initialization */
    IP4_ADDR(&tIpAddr, DEFAULT_IP_ADDR1, DEFAULT_IP_ADDR2,
             DEFAULT_IP_ADDR3, DEFAULT_IP_ADDR4);
    IP4_ADDR(&tNetmask, DEFAULT_MASK_ADDR1, DEFAULT_MASK_ADDR2,
             DEFAULT_MASK_ADDR3, DEFAULT_MASK_ADDR4);
    IP4_ADDR(&tGwIpAddr, DEFAULT_GW_IP_ADDR1, DEFAULT_GW_IP_ADDR2,
             DEFAULT_GW_IP_ADDR3, DEFAULT_GW_IP_ADDR4);

    /* Add the network interface */
    netif_add(&tNetif, &tIpAddr, &tNetmask, &tGwIpAddr, NULL,
              &LWIP_EthernetifInit, &ethernet_input);
    netif_set_default(&tNetif);
    netif_set_up(&tNetif);
    tNetif.flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    /* Open the Upd port and link receive callback */
    ptUdpPcb = udp_new();

    udp_bind(ptUdpPcb, IP_ADDR_ANY, UDP_OPEN_PORT);

    /* Link UDP callback */
    udp_recv(ptUdpPcb, LWIP_UdpReceiveData, (void*)NULL);
}

static err_t LWIP_EthernetifInit(struct netif *ptNetIfHnd)
{
    ptNetIfHnd->output = etharp_output;
    ptNetIfHnd->linkoutput = LWIP_EthernetifOutput;

    return ERR_OK;
}

static err_t LWIP_EthernetifOutput(struct netif *ptNetIfHnd, struct pbuf *ptBuf)
{
    err_t tErr = ERR_OK;

    // TODO uint16_t u16Ret = EOE_SendFrameRequest((UINT16*)ptBuf->payload, (UINT16)ptBuf->tot_len);
    uint16_t u16Ret = 0;
    if (u16Ret != (uint16_t)0U)
    {
        tErr = ERR_IF;
    }

    if (ptBuf != NULL)
    {
        pbuf_free(ptBuf);
        ptBuf = NULL;
    }

    return tErr;
}

void LWIP_EthernetifIntput(void* pData, uint16_t u16SizeBy)
{
    err_t tError;
    struct pbuf* pBuf = NULL;

    /* Allocate data and copy from source */
    pBuf = pbuf_alloc(PBUF_RAW, u16SizeBy, PBUF_POOL);
    memcpy((void*)pBuf->payload, (const void*)pData, u16SizeBy);
    pBuf->len = u16SizeBy;

    tError = tNetif.input(pBuf, &tNetif);

    if (tError != ERR_OK)
    {
        pbuf_free(pBuf);
        pBuf = NULL;
    }
}

void LWIP_SetIpAddress(uint8_t* pu8IpAddr)
{
    ip4_addr_t tNewIpAddr;

    IP4_ADDR(&tNewIpAddr, pu8IpAddr[0], pu8IpAddr[1], pu8IpAddr[2], pu8IpAddr[3]);
    netif_set_ipaddr(&tNetif, &tNewIpAddr);
}

void LWIP_SetNetmask(uint8_t* pu8Netmask)
{
    ip4_addr_t tNewNetmask;

    IP4_ADDR(&tNewNetmask, pu8Netmask[0], pu8Netmask[1], pu8Netmask[2], pu8Netmask[3]);
    netif_set_netmask(&tNetif, &tNewNetmask);
}

void LWIP_SetGwAddress(uint8_t* pu8GwAddr)
{
    ip4_addr_t tNewGwAddr;

    IP4_ADDR(&tNewGwAddr, pu8GwAddr[0], pu8GwAddr[1], pu8GwAddr[2], pu8GwAddr[3]);
    netif_set_gw(&tNetif, &tNewGwAddr);
}

void LWIP_ProcessTimeouts()
{
    /* Handle timeouts */
    sys_check_timeouts();
}



void sys_now()
{
    // HAL_GetTick();
}
