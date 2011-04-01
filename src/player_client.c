#include "player_client.h"
#include "video_play.h"

char * nameserver_init(char * name){
    struct addrinfo hints2, * res2;
    memset(&hints2, 0, sizeof hints2);
	int nameserver_socket = 0;
    hints2.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
    hints2.ai_socktype = SOCK_STREAM;
    hints2.ai_flags = AI_PASSIVE | AI_NUMERICSERV;     // fill in my IP for me
	char * hostname = NAMESERVER_IP;
    char * port = NAMESERVER_LISTEN_PORT_S;

    if((getaddrinfo(hostname, port, &hints2, &res2)) != 0){
        perror("getaddrinfo");
        exit(1);
    }

 	if( (nameserver_socket = socket(res2->ai_family, res2->ai_socktype, res2->ai_protocol)) == -1){
		perror("socket");
		exit(1);
	}


	printf("[PLAYER] Connecting to nameserver...\n");
	if( connect(nameserver_socket, res2->ai_addr, res2->ai_addrlen) == -1 ){
			perror("connect");
			exit(1);
	}

	// Send the find request
	char headerCode = FIND;
	write(nameserver_socket, &headerCode, 1);
	int size = strlen(name);
	write(nameserver_socket, &size, sizeof(int)); 
	write(nameserver_socket, name, size);


	// Receive the response from the newsgroup server
	size = 0;
	read(nameserver_socket, &size, sizeof(int));
	printf("Size: %d\n", size);
	char * ip = malloc(size+1);
	memset(ip, 0, size+1);
	read(nameserver_socket, ip, size);
	ip[size] = '\0';
	printf("IP Address to connect to is %s\n", ip);
	return ip;
}

void client_init(char * ip){

	// Parse the message from the nameserver
	int size = 0;
	printf("Parsing %s\n",ip);
	strstp(ip, "#", &size);
	ip+= size;
	char * hostname = strstp(ip, ":", &size);
	ip+= size;
	char * port = strstp(ip, "#", &size);
	ip+= size;
	
	printf("TARGET: %s:%s [%s]\n", hostname, port, ip);

    struct addrinfo hints2, * res2;
    memset(&hints2, 0, sizeof hints2);
    hints2.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
    hints2.ai_socktype = SOCK_STREAM;
    hints2.ai_flags = AI_PASSIVE | AI_NUMERICSERV;     // fill in my IP for me

    if((getaddrinfo(hostname, port, &hints2, &res2)) != 0){
        perror("getaddrinfo");
        exit(1);
    }

 	if( (player_socket = socket(res2->ai_family, res2->ai_socktype, res2->ai_protocol)) == -1){
		perror("socket");
		exit(1);
	}


	printf("Client connecting to server...\n");
	if( connect(player_socket, res2->ai_addr, res2->ai_addrlen) == -1 ){
			perror("connect");
			exit(1);
	}
/*
    printf("Conected client \n");
	char * buf = malloc( 500 );
	memset(buf, 0 , 500);
	
	struct timeb tp;
	int t1, t2;

	ftime(&tp);
	t1 = (tp.time * 1000) + tp.millitm;
	//printf("Start: %d\n", tp.millitm);

	int dataread = 0;
	if( ( dataread = read(player_socket, buf, 25)) == -1){
		perror("read");
		exit(1);
	}

	ftime(&tp);
	t2 = (tp.time * 1000) + tp.millitm;
	printf("Elapsed Time: %d\n", t2-t1);


	int * t3 = malloc(sizeof(int));
	*t3 = t2-t1;
	if( write(player_socket, t3, sizeof(int))== -1 ){
		perror("write");
		exit(1);
	}

	printf("Received %d bytes: %s\n", dataread, buf);
*/
}

void keyboard_send()
{
	pantilt_packet* pt = NULL;
	while (SDL_PollEvent(&event))   //Poll our SDL key event for any keystrokes.
	{
	switch(event.type) {
		case SDL_KEYDOWN:
			switch(event.key.keysym.sym) {
				case SDLK_LEFT:
					pt = generate_pan_packet(-250);
				break;
      		case SDLK_RIGHT:
					pt = generate_pan_packet(250);
				break;
      		case SDLK_UP:
					pt = generate_tilt_packet(-150);
				break;
      		case SDLK_DOWN:
					pt = generate_tilt_packet(150);
				break;
				default:
				break;
			}
		}
		if(pt != NULL)
		{
			HTTP_packet* http = pantilt_to_network_packet(pt);
			if( write(player_socket, http->message, http->length)== -1 ){
				perror("player_client.c keyboard send write error");
				exit(1);
			}
			destroy_HTTP_packet(http);
			free(pt);
		}
	}
}
