#include "nameserver.h"

int main(){
	init_server();
	message_listen();
}

void init_server(){

	// Open up a socket
	nameserver_socket = socket( AF_INET, SOCK_STREAM, 0 );
	if( nameserver_socket == -1 ){
		perror("socket");
		exit(1);
	}

	// Build sock addr
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port =  htons(NAMESERVER_LISTEN_PORT);

	int addr_size = sizeof(struct sockaddr_in);

	int optval = 1;
    if( (setsockopt(nameserver_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval)) == -1){
        perror("setsockopt");
        exit(1);
    }

	// Bind socket
	if( bind( nameserver_socket, &addr, addr_size ) == -1) {
		perror("bind");
		exit(1);
	}

	// Listen on the socket for connections
	if( listen( nameserver_socket, BACKLOG ) == -1) {
		perror("listen");
		exit(1);
	}

	printf("[NAMESERVER] Server initialization complete.\n");

}

void message_listen(){
	struct sockaddr_storage their_addr;
	int addr_size = sizeof(struct sockaddr_storage);
	int acceptfd;
	while(1){
		memset(&their_addr, 0, sizeof(struct sockaddr_storage));

		printf("Waiting for a connection...\n");
		if(( acceptfd = accept( nameserver_socket, (struct sockaddr *)&their_addr, &addr_size )) == -1 ){
			perror("accept");
			exit(1);
		}
		printf("Connection recieved!\n");
	}
}