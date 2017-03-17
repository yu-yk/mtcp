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
unsigned char mtcp_internal_buffer[5*MAX_BUF_SIZE];
unsigned int global_write_buffer_pointer = 0;
// unsigned int global_send_buffer_pointer = 0;
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
static void send_FIN();
static void send_data();


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
	printf("write position %d\n", global_write_buffer_pointer);
	printf("global_write_buffer_pointer = %d\n", global_write_buffer_pointer);
	memcpy(&mtcp_internal_buffer[global_write_buffer_pointer], buf, buf_len); // keep write to the new space
	global_write_buffer_pointer += buf_len;
	// send signal wake up sending thread
	pthread_cond_signal(&send_thread_sig);

	return buf_len;

}

/* Close Function Call (mtcp Version) */
void mtcp_close(int socket_fd){

	pthread_mutex_lock(&info_mutex);
	global_connection_state = 2;
	pthread_mutex_unlock(&info_mutex);

	pthread_mutex_lock(&app_thread_sig_mutex);
	pthread_cond_wait(&app_thread_sig, &app_thread_sig_mutex);
	pthread_mutex_unlock(&app_thread_sig_mutex);

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
		if (connection_state == 0) {							// three way handshake state
			// perform three way Handshake
			if (last_packet_received == -1) { // send SYN to server
				send_SYN(arg, seq);
				pthread_mutex_lock(&info_mutex);
				global_last_packet_sent = 0;
				pthread_mutex_unlock(&info_mutex);
				// wait for SYN-ACK
				pthread_mutex_lock(&send_thread_sig_mutex);
				pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex);
				pthread_mutex_unlock(&send_thread_sig_mutex);
			} else if (last_packet_received == 1) { // send ACK to server
				send_ACK(arg, seq);
				pthread_mutex_lock(&info_mutex);
				global_last_packet_sent = 4;
				pthread_mutex_unlock(&info_mutex);
				printf("Three Way Handshake established\n");
				// wake up main thread and wait for data transmition
				pthread_cond_signal(&app_thread_sig);
				pthread_mutex_lock(&send_thread_sig_mutex);
				pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex);
				pthread_mutex_unlock(&send_thread_sig_mutex);
			} else {
				printf("three_way_handshake error\n");
			}
		} else if (connection_state == 1) {					// data transmition state
			// send or retransmit data packet
			send_data(arg, seq);
			pthread_mutex_lock(&info_mutex);
			global_last_packet_sent = 5;
			printf("data packet with seq = %d sent\n", seq);
			pthread_mutex_unlock(&info_mutex);
		} else if (connection_state == 2) {					// four way handshake state
			// perform four way handshake
			if (last_packet_received == 4) {					// ACK received
				send_FIN(arg, seq);
				pthread_mutex_lock(&info_mutex);
				global_last_packet_sent = 2;
				pthread_mutex_unlock(&info_mutex);
				// wait for FIN-ACK
				pthread_mutex_lock(&send_thread_sig_mutex);
				pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex);
				pthread_mutex_unlock(&send_thread_sig_mutex);
			} else if (last_packet_received == 3) { 	// FIN-ACK received
				send_ACK(arg, seq);
				pthread_mutex_lock(&info_mutex);
				global_last_packet_sent = 4;
				pthread_mutex_unlock(&info_mutex);
				printf("Four Way Handshake completed\n");
				// wake up main thread
				pthread_cond_signal(&app_thread_sig);
			} else {
				printf("four_way_handshake error\n");
			}
			break;
		} else {
			// unknown error
		}

	}
	return 0;
}

static void *receive_thread(void *server_arg) {
	printf("receive_thread started\n");
	struct arg_list *arg = (struct arg_list *)server_arg;
	socklen_t addrlen = sizeof(arg->server_addr);

	while (1) {
		unsigned int seq;
		unsigned int mode;
		unsigned char buff[4];
		// monitor the socket
		if(recvfrom(arg->socket, buff, sizeof(buff), 0, (struct sockaddr*)&arg->server_addr, &addrlen) < 0) {
			printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
			exit(1);
		}

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
			global_seq = seq;
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&send_thread_sig);
			break;
			case 3: // FIN-ACK
			printf("FIN-ACK recevied\n");
			// when FIN-ACK received
			pthread_mutex_lock(&info_mutex);
			global_last_packet_received = 3;
			global_seq = seq;
			pthread_mutex_unlock(&info_mutex);
			pthread_cond_signal(&send_thread_sig);
			break;
			case 4: // ACK
			printf("ACK recevied\n");
			// when ACK received
			pthread_mutex_lock(&info_mutex);
			global_last_packet_received = 4;
			global_seq = seq;
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

static void send_FIN(struct arg_list *arg, int seq) {
	// construct mtcp FIN header
	mtcpheader FIN;
	FIN.seq = seq;
	FIN.seq = htonl(FIN.seq);
	FIN.mode = '2';
	memcpy(FIN.buffer, &FIN.seq, 4);
	FIN.buffer[0] = FIN.buffer[0] | (FIN.mode << 4);

	printf("try to send FIN\n");
	// send SYN to server
	if(sendto(arg->socket, FIN.buffer, sizeof(FIN.buffer), 0, (struct sockaddr*)&arg->server_addr, (socklen_t)sizeof(arg->server_addr)) <= 0) {
		printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
		exit(1);
	}
	printf("FIN sent\n");
}

static void send_data(struct arg_list *arg, int seq) {
	unsigned char datapacket_buffer[MAX_BUF_SIZE+4]; // MSS
	// construct mtcp data header
	mtcpheader dataheader;
	dataheader.seq = seq;
	dataheader.seq = htonl(dataheader.seq);
	dataheader.mode = '5';
	memcpy(dataheader.buffer, &dataheader.seq, 4);
	memcpy(datapacket_buffer, dataheader.buffer, 4);	// copy header to packet
	memcpy(&datapacket_buffer[4], &mtcp_internal_buffer[seq], MAX_BUF_SIZE);

	printf("try to send data\n");
	//send data to server
	if (sendto(arg->socket, datapacket_buffer, sizeof(datapacket_buffer), 0, (struct sockaddr*)&arg->server_addr, (socklen_t)sizeof(arg->server_addr)) <= 0) {
		printf("Send Error: %s (Error:%d)\n",strerror(errno),errno);
		exit(1);
	}
	printf("Data sent\n");
}
