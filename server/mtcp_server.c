#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "mtcp_server.h"

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
	struct sockaddr_in client_addr;
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
/****server_addr or client_addr?***/

void mtcp_accept(int socket_fd, struct sockaddr_in *client_addr){
	// accept the mtcp call by client
	struct arg_list client_arg;
	client_arg.socket = socket_fd;
	client_arg.client_addr = *client_addr;
	//create thread to handle mtcp_accept()
	int rpc = pthread_create(&recv_thread_pid, NULL, receive_thread, (void *)&client_arg);
	if (rpc < 0) printf("create receving thread error\n");
	int spc = pthread_create(&send_thread_pid, NULL, send_thread, (void *)&client_arg);
	if (spc < 0) printf("create sending thread error\n");


	// waiting
	pthread_mutex_lock(&app_thread_sig_mutex);
	pthread_cond_wait(&app_thread_sig, &app_thread_sig_mutex); // wait
	pthread_mutex_unlock(&app_thread_sig_mutex);

	return;

}

int mtcp_read(int socket_fd, unsigned char *buf, int buf_len){
	//pthread_cond_signal(&send_thread_sig);


	return 0;
}

void mtcp_close(int socket_fd){

}

static void *send_thread(void *client_arg){
	struct arg_list *arg = (struct arg_list *)client_arg;

	/************************************************************************
	*********************** Three Way Handshake *****************************
	*************************************************************************/

	printf("send_thread waiting\n");
	//waiti until SYN accept
	pthread_mutex_lock(&send_thread_sig_mutex);
	pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex); // wait
	pthread_mutex_unlock(&send_thread_sig_mutex);
	printf("send_thread wake\n");

	printf("try send SYN-ACK\n");
	// construct mtcp SYN-ACK header
	mtcpheader SYN_ACK;
	SYN_ACK.seq = 1;
	SYN_ACK.seq = htonl(SYN_ACK.seq);
	SYN_ACK.mode = '1';
	memcpy(SYN_ACK.buffer, &SYN_ACK.seq, 4);
	SYN_ACK.buffer[0] = SYN_ACK.buffer[0] | (SYN_ACK.mode << 4);

	// send SYN-ACK to client
	if(sendto(arg->socket, SYN_ACK.buffer, sizeof(SYN_ACK.buffer), 0, (struct sockaddr*)&arg->client_addr, (socklen_t)sizeof(arg->client_addr)) <=0) {
		printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
		exit(1);
	}
	printf("SYN-ACK sent\n");

	//wait until ACK accept
	pthread_mutex_lock(&send_thread_sig_mutex);
	pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex); // wait
	pthread_mutex_unlock(&send_thread_sig_mutex);

	// wake up main thread
	pthread_cond_signal(&app_thread_sig);

	//wait until ACK accept
	pthread_mutex_lock(&send_thread_sig_mutex);
	pthread_cond_wait(&send_thread_sig, &send_thread_sig_mutex); // wait
	pthread_mutex_unlock(&send_thread_sig_mutex);

	while(1) {

	}
}

static void *receive_thread(void *client_arg){
	struct arg_list *arg = (struct arg_list *)client_arg;
	socklen_t addrlen = sizeof(arg->client_addr);

	// keep monitoring
	while (1) {
		unsigned int seq;
		unsigned int mode;
		unsigned char buff[4];
		
		// monitor for the SYN
		if(recvfrom(arg->socket, buff, sizeof(buff), 0, (struct sockaddr*)&arg->client_addr, &addrlen) < 0) {
			printf("Send Error: %s (Errno:%d)\n",strerror(errno),errno);
			exit(1);
		}

		// decode header
		mode = (buff[0] >> 4);
		buff[0] = buff[0] & 0x0F;
		memcpy(&seq, buff, 4);
		seq = ntohl(seq);

		switch(mode) {
			case 0: // SYN
			// when SYN received
			printf("SYN received\n");
			pthread_cond_signal(&send_thread_sig);
			break;
			case 4: // ACK
			// when ACK received
			printf("ACK received\n");
			pthread_cond_signal(&send_thread_sig);
			break;
			case 5: //DATA
			// when DATA received
			pthread_cond_signal(&send_thread_sig);
			break;
			default:
			printf("receive switch error\n");
		}
	}

}
