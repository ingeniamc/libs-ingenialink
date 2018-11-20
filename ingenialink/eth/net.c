/*
 * MIT License
 *
 * Copyright (c) 2017-2018 Ingenia-CAT S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <winsock2.h>
#include "net.h"
#include "frame.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <windows.h>

#include "ingenialink/err.h"
#include "ingenialink/base/net.h"

WSADATA WSAData;
SOCKET server;
SOCKADDR_IN addr;

/*******************************************************************************
 * Private
 ******************************************************************************/

/**
 * Destroy VIRTUAL network.
 *
 * @param [in] ctx
 *	Context (il_net_t *).
 */
static void eth_net_destroy(void *ctx)
{
	il_eth_net_t *this = ctx;

	il_net_base__deinit(&this->net);

	free(this);
}

static int not_supported(void)
{
	ilerr__set("Functionality not supported");

	return IL_ENOTSUP;
}

bool crc_tabccitt_init_eth = false;
uint16_t crc_tabccitt_eth[256];

/**
 * Compute CRC of the given buffer.
 *
 * @param [in] buf
 *	Buffer.
 * @param [in] sz
 *	Buffer size (bytes).
 *
 * @return
 *	CRC.
 */
static void init_crcccitt_tab_eth( void ) {

	uint16_t i;
	uint16_t j;
	uint16_t crc;
	uint16_t c;

	for (i=0; i<256; i++) {
		crc = 0;
		c   = i << 8;
		for (j=0; j<8; j++) {
			if ( (crc ^ c) & 0x8000 ) crc = ( crc << 1 ) ^ 0x1021;
			else crc =   crc << 1;
			c = c << 1;
		}
		crc_tabccitt_eth[i] = crc;
	}
	crc_tabccitt_init_eth = true;
}

static uint16_t update_crc_ccitt_eth( uint16_t crc, unsigned char c ) {

	if ( ! crc_tabccitt_init_eth ) init_crcccitt_tab_eth();
	return (crc << 8) ^ crc_tabccitt_eth[ ((crc >> 8) ^ (uint16_t) c) & 0x00FF ];

} 

static uint16_t crc_calc_eth(const uint16_t *buf, uint16_t u16Sz)
{
	
	uint16_t crc = 0x0000;
    uint8_t* pu8In = (uint8_t*) buf;
    
	for (uint16_t u16Idx = 0; u16Idx < u16Sz * 2; u16Idx++)
    {
        crc = update_crc_ccitt_eth(crc, pu8In[u16Idx]);
    }
    return crc;
}

/**
 * Process asynchronous statusword messages.
 *
 * @param [in] this
 *	ETH Network.
 * @param [in] frame
 *	IngeniaLink frame.
 */
static void process_statusword(il_eth_net_t *this, uint8_t subnode, uint16_t *data)
{
	il_net_sw_subscriber_lst_t *subs;
	int i;
	uint8_t id;
	uint16_t sw;

	subs = &this->net.sw_subs;

	id = subnode;
	sw = data;

	osal_mutex_lock(subs->lock);

	for (i = 0; i < subs->sz; i++) {
		if (subs->subs[i].id == id && subs->subs[i].cb) {
			void *ctx;

			ctx = subs->subs[i].ctx;
			subs->subs[i].cb(ctx, sw);

			break;
		}
	}

	osal_mutex_unlock(subs->lock);
}

/**
 * Listener thread.
 *
 * @param [in] args
 *	MCB Network (il_eth_net_t *).
 */
int listener_eth(void *args)
{
	
	int r;
	uint16_t buf;
restart:
	int error_count = 0;
	il_eth_net_t *this = to_eth_net(args);
	while(error_count < 10 && this->stop_reconnect == 0) {
		printf("%i\n", error_count);
		osal_mutex_lock(this->net.lock);
		Sleep(5);
		r = net_send(this, 1, 0x0011, NULL, 0, NULL);
		if (r < 0) {
			error_count = error_count + 1;
			goto unlock;
		}
		Sleep(5);
		r = net_recv(this, 1, 0x0011, &buf, 2, NULL, NULL);
		if (r < 0) {
			error_count = error_count + 1;
		}
		else {
			error_count = 0;
		}
		osal_mutex_unlock(this->net.lock);
		unlock:
			osal_mutex_unlock(this->net.lock);
			r = buf;
			Sleep(200);
	}
	if(error_count == 10 && this->stop_reconnect == 0) {
		goto err;
	}
	return 0;
err:
	ilerr__set("Device at %s disconnected\n", this->ip_address);
	r = il_net_reconnect(this);
	if(r == 0) goto restart;
	return 0;
}

static il_net_t *il_eth_net_create(const il_net_opts_t *opts)
{
	il_eth_net_t *this;
 	int r;

 	this = calloc(1, sizeof(*this));
 	if (!this) {
 		ilerr__set("Network allocation failed");
 		return NULL;
 	}

 	/* initialize parent */
 	r = il_net_base__init(&this->net, opts);
 	if (r < 0)
 		goto cleanup_this;

 	this->net.ops = &il_eth_net_ops;
 	this->net.prot = IL_NET_PROT_ETH;
	this->ip_address = opts->port;
	this->port = "23";

	/* setup refcnt */
	this->refcnt = il_utils__refcnt_create(eth_net_destroy, this);
	if (!this->refcnt)
		goto cleanup_refcnt;
	
 	r = il_net_connect(&this->net);
 	if (r < 0)
 		goto cleanup_this;

	return &this->net;

cleanup_refcnt:
	il_utils__refcnt_destroy(this->refcnt);

cleanup_this:
	free(this);

	return NULL;
}

static void il_eth_net_destroy(il_net_t *net)
{
	il_eth_net_t *this = to_eth_net(net);
	il_eth_mon_stop(this);
	osal_thread_join(this->listener, NULL);
	il_utils__refcnt_release(this->refcnt);
	printf("Net destroyed\n");
}

static int il_net_reconnect(il_net_t *net)
{
	il_eth_net_t *this = to_eth_net(net);
	this->stop = 1;
	int r = -1;

	while (r < 0 && this->stop_reconnect == 0)
	{
		printf("Reconnecting...\n");
		server = socket(AF_INET, SOCK_STREAM, 0);

		//set the socket in non-blocking
		unsigned long iMode = 1;
		r = ioctlsocket(server, FIONBIO, &iMode);
		if (r != NO_ERROR)
		{
			printf("ioctlsocket failed with error: %ld\n", r);
		}

		r = connect(server, (SOCKADDR *)&addr, sizeof(addr));
		if (r == SOCKET_ERROR) {
			
			r = WSAGetLastError();

			// check if error was WSAEWOULDBLOCK, where we'll wait
			if (r == WSAEWOULDBLOCK) {
				printf("Attempting to connect.\n");
				fd_set Write, Err;
				TIMEVAL Timeout;
				Timeout.tv_sec = 2;
				Timeout.tv_usec = 0;

				FD_ZERO(&Write);
				FD_ZERO(&Err);
				FD_SET(server, &Write);
				FD_SET(server, &Err);

				

				r = select(0, NULL, &Write, &Err, &Timeout);
				if (r == 0) {
					printf("Timeout during connection\n");
				}
				else {
					if (FD_ISSET(server, &Write)) {
						printf("Reconnected to the Server\n");
						this->stop = 0;
					}
					if (FD_ISSET(server, &Err)) {
						printf("Error reconnecting\n");
					}
				}
			}
			else {
				int last_error = WSAGetLastError();
				printf("Fail connecting to server\n");
			}
			
		}
		
		/*if (r < 0) {
			int last_error = WSAGetLastError();
			printf("Fail connecting to server\n");
		}*/
		else {
			printf("Connected to the Server\n");
			this->stop = 0;
		}
		Sleep(1000);
	}
	r = this->stop_reconnect;
	return r;
}

static int il_eth_net_connect(il_net_t *net, const char *ip)
{
	il_eth_net_t *this = to_eth_net(net);

    int r = 0;

    if ((r = WSAStartup(0x202, &WSAData)) != 0)
    {
        fprintf(stderr,"Server: WSAStartup() failed with error %d\n", r);
        WSACleanup();
        return -1;
    }
    else printf("Server: WSAStartup() is OK.\n");

    server = socket(AF_INET, SOCK_STREAM, 0);

    // addr.sin_addr.s_addr = inet_addr("192.168.150.2");
	addr.sin_addr.s_addr = inet_addr(this->ip_address);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(23);

	r = connect(server, (SOCKADDR *)&addr, sizeof(addr));
    if (r < 0) {
        int last_error = WSAGetLastError();
        printf("Fail connecting to server\n");
        return -1;
    }
	printf("Connected to the Server!");
	// il_net__state_set(&this->net, IL_NET_STATE_CONNECTED);

	/* start listener thread */
	this->stop = 0;
	this->stop_reconnect = 0;

	this->listener = osal_thread_create(listener_eth, this);
	if (!this->listener) {
		ilerr__set("Listener thread creation failed");
		// goto close_ser;
	}

	return 0;
}

il_eth_net_dev_list_t *il_eth_net_dev_list_get()
{
	// TODO: Get slaves scanned
	il_eth_net_dev_list_t *lst = NULL;
	il_eth_net_dev_list_t *prev;


	prev = NULL;
	lst = malloc(sizeof(*lst));
	char *address_ip = "150.1.1.1";
	lst->address_ip = (char *)address_ip;

	return lst;


}

static int il_eth_status_get(il_net_t *net)
{
	il_eth_net_t *this = to_eth_net(net);
	return this->stop;
}

static int il_eth_mon_stop(il_net_t *net)
{
	il_eth_net_t *this = to_eth_net(net);
	this->stop_reconnect = 1;
}

static il_net_servos_list_t *il_eth_net_servos_list_get(
	il_net_t *net, il_net_servos_on_found_t on_found, void *ctx)
{
	int r;
	uint64_t vid;
	il_net_servos_list_t *lst;

	Sleep(2);
	/* try to read the vendor id register to see if a servo is alive */
	r = il_net__read(net, 1, 1, VENDOR_ID_ADDR, &vid, sizeof(vid));
	if (r < 0) {
		return NULL;
	}
	/* create list with one element (id=1) */
	lst = malloc(sizeof(*lst));
	if (!lst) {
		return NULL;
	}	
	lst->next = NULL;
	lst->id = 1;

	if (on_found) {
		on_found(ctx, 1);
	}
		
	return lst;
}

// Monitoring ETH

/**
 * Monitor event callback.
 */
static void on_ser_evt(void *ctx, ser_dev_evt_t evt, const ser_dev_t *dev)
{
	il_eth_net_dev_mon_t *this = ctx;

	if (evt == SER_DEV_EVT_ADDED)
		this->on_evt(this->ctx, IL_NET_DEV_EVT_ADDED, dev->path);
	else
		this->on_evt(this->ctx, IL_NET_DEV_EVT_REMOVED, dev->path);
}

static il_net_dev_mon_t *il_eth_net_dev_mon_create(void)
{
	il_eth_net_dev_mon_t *this;

	this = malloc(sizeof(*this));
	if (!this) {
		ilerr__set("Monitor allocation failed");
		return NULL;
	}

	this->mon.ops = &il_eth_net_dev_mon_ops;
	this->running = 0;

	return &this->mon;
}

static void il_eth_net_dev_mon_destroy(il_net_dev_mon_t *mon)
{
	il_eth_net_dev_mon_t *this = to_eth_mon(mon);

	il_net_dev_mon_stop(mon);

	free(this);
}

static int il_eth_net_dev_mon_start(il_net_dev_mon_t *mon,
				    il_net_dev_on_evt_t on_evt,
				    void *ctx)
{
	il_eth_net_dev_mon_t *this = to_eth_mon(mon);

	if (this->running) {
		ilerr__set("Monitor already running");
		return IL_EALREADY;
	}

	/* store context and bring up monitor */
	this->ctx = ctx;
	this->on_evt = on_evt;
	this->smon = ser_dev_monitor_init(on_ser_evt, this);
	if (!this->smon) {
		ilerr__set("Network device monitor allocation failed (%s)",
			   sererr_last());
		return IL_EFAIL;
	}

	this->running = 1;

	return 0;
}

static void il_eth_net_dev_mon_stop(il_net_dev_mon_t *mon)
{
	il_eth_net_dev_mon_t *this = to_eth_mon(mon);

	if (this->running) {
		ser_dev_monitor_stop(this->smon);
		this->running = 0;
	}
}

static int il_eth_net__read(il_net_t *net, uint16_t id, uint8_t subnode, uint32_t address,
			    void *buf, size_t sz)
{
	il_eth_net_t *this = to_eth_net(net);
	int r;
	(void)id;

	osal_mutex_lock(this->net.lock);
	r = net_send(this, subnode, (uint16_t)address, NULL, 0, 0, net);
	if (r < 0) {
		goto unlock;
	}
	r = net_recv(this, subnode, (uint16_t)address, buf, sz, &net->monitoring_data, net);

unlock:
	osal_mutex_unlock(this->net.lock);

	return r;
}

static int il_eth_net__write(il_net_t *net, uint16_t id, uint8_t subnode, uint32_t address,
			     const void *buf, size_t sz, int confirmed, uint16_t extended)
{
	il_eth_net_t *this = to_eth_net(net);

	int r;

	(void)id;
	(void)confirmed;

	osal_mutex_lock(this->net.lock);


	r = net_send(this, subnode, (uint16_t)address, buf, sz, extended, net);
	if (r < 0)
		goto unlock;

	r = net_recv(this, subnode, (uint16_t)address, NULL, 0, NULL, NULL);

unlock:
	osal_mutex_unlock(this->net.lock);

	return r;
}

typedef union
{
	uint64_t u64;
	uint16_t u16[4];
} UINT_UNION_T;

static int net_send(il_eth_net_t *this, uint8_t subnode, uint16_t address, const void *data,
		    size_t sz, uint16_t extended, il_net_t *net)
{	
	int finished = 0;
	uint8_t cmd;

	cmd = sz ? ETH_MCB_CMD_WRITE : ETH_MCB_CMD_READ;

	// (void)ser_flush(this->ser, SER_QUEUE_ALL);

	while (!finished) {
		int r;
		uint16_t frame[ETH_MCB_FRAME_SZ];
		uint16_t hdr_h, hdr_l, crc;
		size_t chunk_sz;

		/* header */
		// hdr_h = (MCB_SUBNODE_MOCO << 12) | (MCB_NODE_DFLT);
		hdr_h = (ETH_MCB_NODE_DFLT << 4) | (subnode);
		*(uint16_t *)&frame[ETH_MCB_HDR_H_POS] = hdr_h;
		hdr_l = (address << 4) | (cmd << 1) | (extended);
		*(uint16_t *)&frame[ETH_MCB_HDR_L_POS] = hdr_l;

		/* cfg_data */
		uint64_t d = 0;
		/* Check if frame is extended */
		if (extended == 1) {
			d = net->disturbance_data_size;
		}
		else {
			if (sz > 0) {
				memcpy(&d, data, sz);
			}
		}
		UINT_UNION_T u = { .u64 = d };
		memcpy(&frame[ETH_MCB_DATA_POS], &u.u16[0], 8);

		/* crc */
		crc = crc_calc_eth(frame, ETH_MCB_CRC_POS);
		frame[ETH_MCB_CRC_POS] = crc;

		/* send frame */
		if (extended == 1) {
			uint16_t frame_size = sizeof(uint16_t) * ETH_MCB_FRAME_SZ;
			uint16_t extended_frame[(sizeof(uint16_t) * ETH_MCB_FRAME_SZ) + 2048];
			memcpy(&extended_frame[0], frame, frame_size);
			memcpy(&extended_frame[ETH_MCB_FRAME_SZ], net->disturbance_data, 2048);
			r = send(server, (const char*)&extended_frame[0], net->disturbance_data_size + frame_size, 0);
			if (r < 0)
				return ilerr__ser(r);
		}
		else {
			r = send(server, (const char*)&frame[0], sizeof(frame), 0);
			if (r < 0)
				return ilerr__ser(r);		
		}
		finished = 1;
		/*if (extended == 1) {
			r = send(server, (const char*)&net->disturbance_data[0], net->disturbance_data_size, 0);
			if (r < 0)
				return ilerr__ser(r);
		}*/
	}

	return 0;
}

static int net_recv(il_eth_net_t *this, uint8_t subnode, uint16_t address, uint8_t *buf,
		    size_t sz, uint16_t monitoringArray[], il_net_t *net)
{
	int finished = 0;
	size_t pending_sz = sz;

	/*while (!finished) {*/
	uint16_t frame[7];
	size_t block_sz = 0;
	uint16_t crc, hdr_l;
	uint8_t *pBuf = (uint8_t*) &frame;
	uint8_t extended_bit = 0;

	Sleep(5);
	/* read next frame */
	int r = 0;
	r = recv(server, (char*)&pBuf[0], sizeof(frame), 0);
	
	/* process frame: validate CRC, address, ACK */
	crc = *(uint16_t *)&frame[6];
	uint16_t crc_res = crc_calc_eth((uint16_t *)frame, 6);
	if (crc_res != crc) {
		ilerr__set("Communications error (CRC mismatch)");
		return IL_EIO;
	}

	/* TODO: Check subnode */

	/* Check ACK */
	hdr_l = *(uint16_t *)&frame[ETH_MCB_HDR_L_POS];
	int cmd = (hdr_l & ETH_MCB_CMD_MSK) >> ETH_MCB_CMD_POS;
	if (cmd != ETH_MCB_CMD_ACK) {
		uint32_t err;

		err = __swap_be_32(*(uint32_t *)&frame[ETH_MCB_DATA_POS]);

		ilerr__set("Communications error (NACK -> %08x)", err);
		return IL_EIO;
	}
	extended_bit = (hdr_l & ETH_MCB_PENDING_MSK) >> ETH_MCB_PENDING_POS;
	if (extended_bit == 1) {
		/* Read size of data */
		memcpy(buf, &(frame[ETH_MCB_DATA_POS]), 2);
		net->monitoring_data_size = *(uint16_t*)buf;
		uint8_t *pBufMonitoring = (uint8_t*)monitoringArray;
		r = recv(server, (uint8_t*)monitoringArray, net->monitoring_data_size, 0);
	}
	else {
		memcpy(buf, &(frame[ETH_MCB_DATA_POS]), sz);
	}
	
	return 0;
}


/** MCB network operations. */
const il_eth_net_ops_t il_eth_net_ops = {
	/* internal */
	._read = il_eth_net__read,
	._write = il_eth_net__write,
	._sw_subscribe = il_net_base__sw_subscribe,
	._sw_unsubscribe = il_net_base__sw_unsubscribe,
	._emcy_subscribe = il_net_base__emcy_subscribe,
	._emcy_unsubscribe = il_net_base__emcy_unsubscribe,
	/* public */
	.create = il_eth_net_create,
	.destroy = il_eth_net_destroy,
	.connect = il_eth_net_connect,
	// .devs_list_get = il_eth_net_dev_list_get,
	.servos_list_get = il_eth_net_servos_list_get,
	.status_get = il_eth_status_get,
	.mon_stop = il_eth_mon_stop,
};

/** MCB network device monitor operations. */
const il_net_dev_mon_ops_t il_eth_net_dev_mon_ops = {
	.create = il_eth_net_dev_mon_create,
	.destroy = il_eth_net_dev_mon_destroy,
	.start = il_eth_net_dev_mon_start,
	.stop = il_eth_net_dev_mon_stop
};
