#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "mtcp_client.h"
#include <errno.h>
#include <mtcp_common.h>


/* -------------------- Global Variables -------------------- */
unsigned char *mtcp_internal_buffer[MAX_BUF_SIZE];
unsigned int global_connection_state = 0;
unsigned int global_last_packet_received = -1;
unsigned int global_last_packet_sent = -1;
unsigned int global_seq = 0;

typedef struct mtcpheaders
{
	unsigned int seq;			// sequence number
	unsigned char mode;			// mode code
	unsigned char buffer[4]; 	// header byte array

} mtcpheader;

struct arg_list
{
	int socket;
	struct sockaddr_in server_addr;
};

/* ThreadID for Sending Thread and Receiving Thread */
static pthread_t send_thread_pid;
static pthread_t recv_thread_pid;

static pthread_cond_t app_thread_sig = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t app_thread_sig_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t send_thread_sig = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t send_thread_sig_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t info_mutex = PTHREAD_MUTEX_INITIALIZER;

/* The Sending Thread and Receive Thread Function */
static void *send_thread();
static void *receive_thread();

/* Three Way Handshake Function*/
static void send_SYN();
static void send_ACK();

/* Data Transmition Function */
static void send_data(int seq);

/* Four Way Handshake Function */
static void four_way_handshake();

/* Connect Function Call (mtcp Version) */
void mtcp_connect(int socket_fd, struct sockaddr_in *server_addr){

	// init connect server arg
	struct arg_list server_arg;
	server_arg.socket = socket_fd;
	server_arg.server_addr = *server_addr;

	// create send_thread and receive_thread
	int spc = pthread_create(&send_thread_pid, NULL, send_thread, (void *)&server_arg);
	if (spc < 0) printf("create sending thread error\n");
	int rpc = pthread_create(&recv_thread_pid, NULL, receive_thread, (void *)&server_arg);
	if (rpc < 0) printf("create receivng thread error\n");

	pthread_mutex_lock(&app_thread_sig_mutex);
	pthread_cond_wait(&app_thread_sig, &app_thread_sig_mutex);
	pthread_mutex_unlock(&app_thread_sig_mutex);

	return;
}

/* Write Function Call (mtcp Version) */
int mtcp_write(int socket_fd, unsigned char *buf, int buf_len){
	// write message to internal buffer
	memcpy(mtcp_internal_buffer, buf, buf_len);

	// send signal wake up sending thread
	pthread_cond_signal(&send_thread_sig);


	return 0;
}

/* Close Function Call (mtcp Version) */
void mtcp_close(int socket_fd){

}

static void *send_thread(void *server_arg) {
	printf("send_thread started\n");
	struct arg_list *arg = (struct arg_list *)server_arg;
	int connection_state;
	// 0 = three way handshake
	// 1 = data transmition
	// 2 = four way handshake
	int last_packet_received;
	// 0 = SYN
	// 1 = SYN-ACK
	// 2 = FIN
	// 3 = FIN-ACK
	// 4 = ACK
	// 5 = DATA
	int seq;

	while (1) {

		sleep(1); // time out for retransmition

		// check state
		pthread_mutex_lock(&info_mutex);
		if (global_connection_state == 0) {
			connection_state = 0;												// During three way handshake
		} else if (global_connection_state == 1) {
			connection_state = 1;												// During data transmition
		} else if (global_connection_state == 2) {
			connection_state = 2;												// During four way handshake
		} else {
			connection_state = -1;
		}

		if (global_last_packet_received == 1) {
			last_packet_received = 1;											// SYN-ACK received
		} else if (global_last_packet_received == 4) {
			last_packet_received = 4;											// ACK received
		} else if (global_last_packet_received == 3) {
			last_packet_received = 3;											// FIN-ACK received
		} else {
			last_packet_received = -1;										// nothing received yet
		}

		seq = global_seq;
		pthread_mutex_unlock(&info_mutex);


		// send packet
		if (connection_state == 0) {
			// perform three way Handshake
			if (last_packet_received == -1) { // send SYN to server
				send_SYN(arg, seq);
				pthread_mutex_lock(&info_mutex);
				global_last_packet_sent = 0;
				pthread_mutex_unlock(&info_mutex);
			} else if (last_flag_received == 1) { // send ACK to server
				send_ACK(arg, seq);
				pthread_mutex_lock(&info_mutex);
				global_last_packet_sent = 4;
				pthread_mutex_unlock(&info_mutex);
				printf("Three Way Handshake established\n");
				// wake up main thread
				pthread_cond_signal(&app_thread_sig);

				pthread_mutex_lock(&send_thread_sig_mutex);
				pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex);
				pthread_mutex_unlock(&send_thread_sig_mutex);
			} else {
				printf("three_way_handshake error\n");
			}
		} else if (connection_state == 1) {
			// send or retransmit data packet
		} else if (connection_state == 2) {
			// perform four way handshake
			break;
		} else {
			// unknown error
		}

	}

}

static void *receive_thread(void *server_arg) {
	printf("receive_thread started\n");
	struct arg_list *arg = (struct arg_list *)server_arg;
	socklen_t addrlen = sizeof(arg->server_addr);
	int connection_state;
	// 0 = three way handshake
	// 1 = data transmition
	// 2 = four way handshake
	int last_packet_sent;
	// 0 = SYN
	// 1 = SYN-ACK
	// 2 = FIN
	// 3 = FIN-ACK
	// 4 = ACK
	// 5 = DATA
	int seq;

	while (1) {
		// monitor the socket
		// monitor for the SYN-ACK
		if(recvfrom(arg->socket, buff, sizeof(buff), 0, (struct sockaddr*)&arg->server_addr, &addrlen) < 0) {
			printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
			exit(1);
		}
		unsigned int seq;
		unsigned int mode;
		unsigned char buff[4];

		// decode header
		mode = buff[0] >> 4;
		buff[0] = buff[0] & 0x0F;
		memcpy(&seq, buff, 4);
		seq = ntohl(seq);

		switch(mode) {
			case 1: // SYN-ACK
			printf("SYN-ACK recevied\n");
			// when SYN-ACK received
			pthread_mutex_lock(&info_mutex);
			global_last_packet_received = 1;
			pthread_mutex_unlock(&info_mutex);
			break;
			case 4: // ACK
			printf("ACK recevied\n");
			// when ACK received
			pthread_mutex_lock(&info_mutex);
			global_last_packet_received = 4;
			pthread_mutex_unlock(&info_mutex);
			break;
			default:
			printf("receive switch error\n");
		}

		// check and update state
		pthread_mutex_lock(&info_mutex);
		// check last packet sent
		if (global_last_packet_sent == 0) {					// SYN sent
			global_connection_state = 0;							// state remain three_way_handshake
		} else if (global_last_packet_sent == 4) {	// ACK sent
			global_connection_state = 1;							// state change to data transmition
		} else if (global_last_packet_sent == 5) {	// DATA sent
			global_connection_state = 1;							// state remian data transmition
		} else if (global_last_packet_sent == 2) {	// FIN sent
			global_connection_state = 2;							// state remian four_way_handshake
		} else if (global_last_packet_sent == -1) {
			global_connection_state = -1;
		}
		seq = global_seq;
		pthread_mutex_unlock(&info_mutex);

		// if 4 way finished break

	}

}

static void send_SYN(struct arg_list *arg, int seq) {
	// construct mtcp SYN header
	mtcpheader SYN;
	SYN.seq = seq;
	SYN.seq = htonl(SYN.seq);
	SYN.mode = '0';
	memcpy(SYN.buffer, &SYN.seq, 4);
	SYN.buffer[0] = SYN.buffer[0] | (SYN.mode << 4);

	printf("try to send SYN \n");
	// send SYN to server
	if(sendto(arg->socket, SYN.buffer, sizeof(SYN.buffer), 0, (struct sockaddr*)&arg->server_addr, (socklen_t)sizeof(arg->server_addr)) <= 0) {
		printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
		exit(1);
	}
	printf("SYN sent\n");
}

static void send_ACK(struct arg_list *arg, int seq) {
	// construct mtcp ACK header
	mtcpheader ACK;
	ACK.seq = seq;
	ACK.seq = htonl(ACK.seq);
	ACK.mode = '4';
	memcpy(ACK.buffer, &ACK.seq, 4);
	ACK.buffer[0] = ACK.buffer[0] | (ACK.mode << 4);

	printf("try to send ACK \n");
	// send ACK to server
	if(sendto(arg->socket, ACK.buffer, sizeof(ACK.buffer), 0, (struct sockaddr*)&arg->server_addr, (socklen_t)sizeof(arg->server_addr)) <= 0) {
		printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
		exit(1);
	}
	printf("ACK sent\n");
}

static void three_way_handshake(struct arg_list *arg) {
	/*************************************************************************
	*********************** Three Way Handshake *****************************
	*************************************************************************/

	// // construct mtcp SYN header
	// mtcpheader SYN;
	// SYN.seq = 0;
	// SYN.seq = htonl(SYN.seq);
	// SYN.mode = '0';
	// memcpy(SYN.buffer, &SYN.seq, 4);
	// SYN.buffer[0] = SYN.buffer[0] | (SYN.mode << 4);
	//
	// printf("try to send SYN \n");
	// // send SYN to server
	// if(sendto(arg->socket, SYN.buffer, sizeof(SYN.buffer), 0, (struct sockaddr*)&arg->server_addr, (socklen_t)sizeof(arg->server_addr)) <= 0) {
	// 	printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
	// 	exit(1);
	// }
	// printf("SYN sent\n");
	//
	// // waiting again
	// printf("send_thread waiting\n");
	// pthread_mutex_lock(&send_thread_sig_mutex);
	// pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex); // wait
	// pthread_mutex_unlock(&send_thread_sig_mutex);

	// // construct mtcp ACK header
	// mtcpheader ACK;
	// ACK.seq = 1;
	// ACK.seq = htonl(ACK.seq);
	// ACK.mode = '4';
	// memcpy(ACK.buffer, &ACK.seq, 4);
	// ACK.buffer[0] = ACK.buffer[0] | (ACK.mode << 4);
	//
	// printf("try to send ACK \n");
	// // send ACK to server
	// if(sendto(arg->socket, ACK.buffer, sizeof(ACK.buffer), 0, (struct sockaddr*)&arg->server_addr, (socklen_t)sizeof(arg->server_addr)) <= 0) {
	// 	printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
	// 	exit(1);
	// }
	// printf("ACK sent\n");
	//
	// // wake up main thread
	// pthread_cond_signal(&app_thread_sig);
	// printf("Three Way Handshake established\n");

	/*************************************************************************
	********************* End of Three Way Handshake ************************
	*************************************************************************/
}

static void send_data(struct arg_list *arg, int seq) {
	/* code */
}

static void four_way_handshake(struct arg_list *arg) {
	/* code */
}
