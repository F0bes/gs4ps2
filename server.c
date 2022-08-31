#include "common.h"
#include "server.h"
#include "gs_glue.h"
#include "crc8.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <sys/time.h>
#include <ps2ip.h>
#include <dma.h>
#include <errno.h>

static s32 tcp_client;
static s32 tcp_server;

ee_thread_t thread_server;
s32 thread_server_id;

void interpret_command(u8 cmd, u8 size)
{
	switch (cmd)
	{
		case SERVER_CMD_VER:
			dprint("Client requested version\n");
			send(tcp_client, "0.2", 4, 0);
			break;
		case SERVER_TRANSFER:
		{
			dprint("Client requested transfer\n");

			u8 CRC;
			lwip_recv(tcp_client, &CRC, sizeof(u8), 0);
			u32 batched_transfer_size = 0;
			lwip_recv(tcp_client, &batched_transfer_size, sizeof(u32), 0);
			dprint("Batched transfer CRC: %X, Size: %X\n", CRC, batched_transfer_size);
			u8* batched_transfer_buffer = (u8*)aligned_alloc(64, batched_transfer_size);

			u32 bytes_read = 0;
			do
			{
				u32 recv_ret = lwip_recv(tcp_client, batched_transfer_buffer + bytes_read, batched_transfer_size - bytes_read, 0);
				if (recv_ret == -1)
					return;
				bytes_read += recv_ret;
				if (*(s32*)CFG_VALS[CFG_OPT_NET_DBG_MSG])
					dprint("received: %d\nrem: %d\n", recv_ret, batched_transfer_size - bytes_read);
			} while (bytes_read < batched_transfer_size);

			u8 calc_CRC = 0;
			crc8_buffer(&calc_CRC, batched_transfer_buffer, batched_transfer_size);
			dprint("Calculated transfer CRC: %X\n", calc_CRC);

			u8 resp = SERVER_OK;
			if (CRC != calc_CRC)
			{
				dprint("CRC mismatch!\n");
				resp = SERVER_RETRY;
				lwip_send(tcp_client, &resp, sizeof(u8), 0);
				break;
			}
			lwip_send(tcp_client, &resp, sizeof(u8), 0);

			u8* trans_ptr = batched_transfer_buffer;
			gs_glue_transfer(trans_ptr, batched_transfer_size);

			free(batched_transfer_buffer);
			break;
		}
		case SERVER_WAIT_VSYNC:
		{
			dprint("Client requested wait vsync\n");
			u8 field;
			lwip_recv(tcp_client, &field, sizeof(u8), 0);
			if (*(s32*)CFG_VALS[CFG_OPT_FRAME_DUMP])
			{
				gs_vsync_data_header circuit_header[2];
				u32 circuit_data_ptr[2];

				u32 circuits = gs_glue_vsync(&circuit_header[0], &circuit_data_ptr[0], &circuit_header[1], &circuit_data_ptr[1]);

				u8 resp = SERVER_OK_FRAME;
				lwip_send(tcp_client, &resp, sizeof(u8), 0);

				lwip_send(tcp_client, &circuits, sizeof(circuits), 0);

				for (int i = 0; i <= 1; i++)
				{
					const u32 circuit = i + 1;
					if (!(circuits & circuit))
					{
						continue;
					}

					lwip_send(tcp_client, &circuit_header[i].Circuit, sizeof(circuit_header[i].Circuit), 0);
					lwip_send(tcp_client, &circuit_header[i].PSM, sizeof(circuit_header[i].PSM), 0);
					lwip_send(tcp_client, &circuit_header[i].Width, sizeof(circuit_header[i].Width), 0);
					lwip_send(tcp_client, &circuit_header[i].Height, sizeof(circuit_header[i].Height), 0);
					lwip_send(tcp_client, &circuit_header[i].Bytes, sizeof(circuit_header[i].Bytes), 0);

					u32 sent_cnt = 0;
					do
					{
						u32 sent = lwip_send(tcp_client, (u8*)(circuit_data_ptr[i]) + sent_cnt,
							circuit_header[i].Bytes - sent_cnt, 0);
						sent_cnt += sent;
					} while (sent_cnt < circuit_header[i].Bytes);
					free((u8*)circuit_data_ptr[i]);
				}
			}
			else
			{
				u8 resp = SERVER_OK;
				lwip_send(tcp_client, &resp, sizeof(u8), 0);
			}

			break;
		}
		case SERVER_READ_FIFO:
			dprint("Client requested read fifo\n");
			u32 size = 0;
			lwip_recv(tcp_client, &size, sizeof(u32), 0);
			free(gs_glue_read_fifo(size));
			u8 resp = SERVER_OK;
			lwip_send(tcp_client, &resp, sizeof(u8), 0);
			break;
		case SERVER_SET_REG:
		{
			dprint("Client requested set registers\n");
			u8 CRC = 0;
			lwip_recv(tcp_client, &CRC, sizeof(u8), 0);
			gs_registers_packet* packet = (gs_registers_packet*)aligned_alloc(64, sizeof(gs_registers_packet));
			u32 size = sizeof(gs_registers_packet);
			u32 received_cnt = 0;
			do
			{
				u32 recv_ret = lwip_recv(tcp_client, (u8*)packet + received_cnt, size - received_cnt, 0);
				if (recv_ret == -1)
					return;
				received_cnt += recv_ret;
				if (*(s32*)CFG_VALS[CFG_OPT_NET_DBG_MSG])
					dprint("received: %d\nrem: %d\n", recv_ret, size - received_cnt);
			} while (received_cnt < size);
			u8 calc_CRC = 0;
			crc8_buffer(&calc_CRC, (u8*)packet, sizeof(gs_registers_packet));

			dprint("Calc CRC: %X, client CRC: %X\n", calc_CRC, CRC);

			u8 resp = SERVER_OK;
			if (CRC != calc_CRC)
			{
				dprint("CRC mismatch!\n");
				resp = SERVER_RETRY;
				lwip_send(tcp_client, &resp, sizeof(u8), 0);
				break;
			}
			lwip_send(tcp_client, &resp, sizeof(u8), 0);
			gs_glue_registers(packet);
			free(packet);
		}
		break;
		case SERVER_FREEZE:
		{
			dprint("Client requested freeze\n");
			u8 CRC;
			lwip_recv(tcp_client, &CRC, sizeof(u8), 0);
			u32 freeze_size;
			u32 recv_ret = lwip_recv(tcp_client, &freeze_size, sizeof(u32), 0);
			dprint("savestate(freeze) is %d bytes (CRC %X)\n", freeze_size, CRC);
			// HACK: I NEED THE FIRST 4 BYTES AND THEN THE QWORDS AFTER IT
			// GCC ALLOWS UNALIGNED DWORD LOADS WHICH END UP CAUSING AN ADDRESS
			// EXCEPTION. SO READ THE FIRST 4 BYTES, AND THEN FILL THE FREEZE DATA
			u32 freeze_ver;
			lwip_recv(tcp_client, &freeze_ver, sizeof(u32), 0);
			u8* freeze_data = (u8*)aligned_alloc(64, freeze_size);
			u32 received_cnt = 0;
			do
			{
				recv_ret = lwip_recv(tcp_client, freeze_data + received_cnt, freeze_size - received_cnt, 0);
				if (recv_ret == -1)
					return;
				if (*(s32*)CFG_VALS[CFG_OPT_NET_DBG_MSG])
					dprint("received: %d\nrem: %d\n", recv_ret, freeze_size - received_cnt);
				received_cnt += recv_ret;
			} while (received_cnt < freeze_size);

			u8 calc_CRC = 0;
			crc8_buffer(&calc_CRC, freeze_data, freeze_size);
			dprint("Calculated FREEZE CRC: %X\n", calc_CRC);

			resp = SERVER_OK;
			if (CRC != calc_CRC)
			{
				dprint("CRC mismatch!\n");
				resp = SERVER_RETRY;
				lwip_send(tcp_client, &resp, sizeof(u8), 0);
				break;
			}
			lwip_send(tcp_client, &resp, sizeof(u8), 0);
			gs_glue_freeze(freeze_data, freeze_ver);
			free(freeze_data);
			break;
		}
		case SERVER_SHUTDOWN:
			//		Exit(0);
			dprint("client requested shutdown\n");
			resp = SERVER_OK;
			lwip_send(tcp_client, &resp, sizeof(u8), 0);
			break;
		default:
			dprint("Unknown command: %d\n", cmd);
			break;
	}
}

void thread_server_func(void* arg)
{
	dprint("Server thread is alive\n");
wait_client:
	dprint("Waiting for client\n");

	struct sockaddr_in addr_client;
	s32 sz_addr_client = sizeof(addr_client);
	while (1)
	{
		tcp_client = accept(tcp_server, (struct sockaddr*)&addr_client, &sz_addr_client);
		if (tcp_client >= 0)
		{
			dprint("Client connected\n");
#ifdef UDP_OUT
			common_client_dest = addr_client.sin_addr.s_addr;
#endif
			break;
		}
		else
		{
			dprint("Client not connected\n");
		}
	}
	u8 client_ip[4];
	client_ip[0] = ip4_addr1((struct ip4_addr*)&addr_client.sin_addr.s_addr);
	client_ip[1] = ip4_addr2((struct ip4_addr*)&addr_client.sin_addr.s_addr);
	client_ip[2] = ip4_addr3((struct ip4_addr*)&addr_client.sin_addr.s_addr);
	client_ip[3] = ip4_addr4((struct ip4_addr*)&addr_client.sin_addr.s_addr);

	dprint("Client ip address is %d.%d.%d.%d\n", client_ip[0], client_ip[1], client_ip[2], client_ip[3]);

	while (1)
	{
		u8 buffer;
		s32 result = recv(tcp_client, &buffer, 1, 0);
		if (result > 0)
		{
			dprint("Client sent: %d\n", buffer);
			interpret_command(buffer, result);
		}
		else
		{
			dprint("Client disconnected\n");
			goto wait_client;
		}
	}
}

s32 server_init(void)
{
	init_crc8();
	dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
	gs_glue_init();

	tcp_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (tcp_server < 0)
	{
		dprint("Failed to create server socket. (%d)\n", errno);
		return -1;
	}
	// Bind the server
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT_TCP);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(tcp_server, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
	{
		dprint("Failed to bind server socket. (%d)", errno);
		return -1;
	}

	if (listen(tcp_server, 2) < 0)
	{
		dprint("Failed to listen on server socket. (%d)\n", errno);
		return -1;
	}

	thread_server.func = &thread_server_func;
	thread_server.attr = 0;
	thread_server.option = 0;
	thread_server.stack = aligned_alloc(16, 0x10000);
	thread_server.stack_size = 0x10000;
	thread_server.gp_reg = &_gp;
	thread_server.initial_priority = 0x50; // idk

	thread_server_id = CreateThread(&thread_server);
	if (thread_server_id < 0)
	{
		dprint("Failed to create server thread.\n");
		return -1;
	}

	StartThread(thread_server_id, NULL);
	ResumeThread(thread_server_id);
	return 0;
}
