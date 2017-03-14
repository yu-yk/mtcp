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

#ifndef EOK
#define  EOK 6
#endif

/* -------------------- Global Variables -------------------- */
typedef struct mtcpheaders
{
	unsigned int seq;			// sequence number
	unsigned char mode;			// mode code
	unsigned char buffer[4]; 	// header byte array

} mtcpheader;

struct arg_list
{
	int socket;
	struct sockaddr_in *server_addr;
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

/* Connect Function Call (mtcp Version) */
void mtcp_connect(int socket_fd, struct sockaddr_in *server_addr){

	// init connect server arg
	struct arg_list server_arg;
	server_arg.socket = socket_fd;
	server_arg.server_addr = server_addr;


	int spc = pthread_create(&send_thread_pid, NULL, send_thread, (void *)&server_arg);
	printf("send_thread created\n");
	if (spc < 0) printf("create sending thread error\n");
	int rpc = pthread_create(&recv_thread_pid, NULL, receive_thread, (void *)&server_arg);
	printf("receive_thread created\n");
	if (rpc < 0) printf("create receivng thread error\n");

	// sleep 1 second for the thread creation
	sleep(1);
	// wake up sending thread
	pthread_cond_signal(&send_thread_sig);
	// then waiting
	pthread_mutex_lock(&app_thread_sig_mutex);
	pthread_cond_wait(&app_thread_sig, &app_thread_sig_mutex);
	pthread_mutex_unlock(&app_thread_sig_mutex);

	return;
}

/* Write Function Call (mtcp Version) */
int mtcp_write(int socket_fd, unsigned char *buf, int buf_len){

	return 0;
}

/* Close Function Call (mtcp Version) */
void mtcp_close(int socket_fd){

}

static void *send_thread(void *server_arg){
	printf("send_thread started\n");
	struct arg_list *arg = (struct arg_list *)server_arg;
	/*************************************************************************
	*********************** Three Way Handshake *****************************
	*************************************************************************/

	// waiting
	pthread_mutex_lock(&send_thread_sig_mutex);
	printf("send_thread waiting\n");
	pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex); // wait
	pthread_mutex_unlock(&send_thread_sig_mutex);
	printf("send_thread woke\n");

	// construct mtcp SYN header
	mtcpheader SYN;
	SYN.seq = 0;
	SYN.mode = '0';
	memcpy(SYN.buffer, &SYN.seq, 4);
	SYN.buffer[0] = SYN.buffer[0] | (SYN.mode << 4);


	printf("try to send SYN \n");
	// send SYN to server
	sendto(arg->socket, SYN.buffer, sizeof(SYN.buffer), 0, (struct sockaddr*)arg->server_addr, (socklen_t)sizeof(arg->server_addr));
	printf("SYN sent\n");

	// waiting again
	pthread_mutex_lock(&send_thread_sig_mutex);
	pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex); // wait
	pthread_mutex_unlock(&send_thread_sig_mutex);

	// construct mtcp ACK header
	mtcpheader ACK;
	ACK.seq = 1;
	ACK.mode = '4';
	memcpy(ACK.buffer, &ACK.seq, 4);
	ACK.buffer[0] = ACK.buffer[0] | (ACK.mode << 4);

	printf("try to send ACK \n");
	// send ACK to server
	sendto(arg->socket, ACK.buffer, sizeof(ACK.buffer), 0, (struct sockaddr*)arg->server_addr, (socklen_t)sizeof(arg->server_addr));
	printf("ACK sent\n");

	// wake up main thread
	pthread_cond_signal(&app_thread_sig);
	/*************************************************************************
	********************* End of Three Way Handshake ************************
	*************************************************************************/

	while (1) {
		/* wait for send data */
	}

}

static void *receive_thread(void *server_arg){
	printf("receive_thread started\n");

	unsigned char buf[4];
	struct arg_list *arg = (struct arg_list *)server_arg;
	socklen_t addrlen = sizeof(arg->server_addr);

	// keep monitoring
	while (1) {
		// monitor for the SYN-ACK
		errno = EOK;
		if(recvfrom(arg->socket, buf, sizeof(buf), 0, (struct sockaddr*)arg->server_addr, &addrlen) < 0)
		{
			printf("error%d\n", errno);
			printf("%s\n", strerror(errno));
			printf("receive error\n");
			exit(1);
		}

		// decode header
		mtcpheader header;
		header.mode = buf[0] >> 4;
		header.buffer[0] = buf[0] & 0x0F;
		memcpy(&header.seq, header.buffer, 4);
		header.seq = ntohl(header.seq);

		switch(header.mode) {
			case '1': // SYN-ACK
			printf("header mode: %c\n", header.mode);
			printf("SYN-ACK recevied\n");
			// when SYN-ACK received
			pthread_cond_signal(&send_thread_sig);
			break;
			case '4': // ACK
			printf("header mode: %c\n", header.mode);
			printf("ACK recevied\n");
			// when ACK received
			pthread_cond_signal(&send_thread_sig);
			break;
			default:
			printf("receive switch error\n");
		}
	}

}
