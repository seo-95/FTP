#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "../errlib.h"
#include "../FTP_lib.h"

char *prog_name;
server_t server;
int conn_id;

int fsend(conn_t* conn, char* fname);

void sig_manager(int sig)
{
	int count;
	if(sig == SIGINT)
	{
		/* deallocates all the common structure. If some files are open during the SIGNINT catching,
			 it's not possible to close it */
			shut_down(&server);
			printf(" Server shutted down\n");
			exit(0);
	}
	if(sig == SIGPIPE)
	{
		server_close_conn(&server, conn_id);
		printf("BROKEN PIPE . . . connection closed\n");
	}
}



int main(int argc, char *argv[])
{

	int result,
		  quit;
	char fname[MAX_FILE_NAME];

	signal(SIGINT, sig_manager);
	signal(SIGPIPE, sig_manager);

	if(argc != 2) /* prog name + port# */
		err_quit("param: <port#>");

	result = server_setup(&server);
	if(result == -1)
	{
		shut_down(&server);
		err_quit("Not enough memory for the server structure. Server it's not running");
	}

	else
		if(result == -2)
		{
			printf("Error - %s\n", server.error);
			shut_down(&server);
			return -1;
		}

	/* starts to listen on a particul port */
	result = listen_on(&server, argv[1]);
	if(result == -1)
	{
		shut_down(&server);
		err_quit("Error - %s", server.error);
	}

	else
		if(result == -2)
		{
			printf("Error - %s\n", server.connections[0].err_msg);
			shut_down(&server);
			err_quit("Exiting . . .");
		}
new_req:
    while(1)
    {
        memset(fname, '\0', MAX_FILE_NAME * sizeof(char));
        printf("Server listening on port %s\n", argv[1]);
				result = accept_new_request(&server, &conn_id);
				if(result < 0)
				{
					printf("Error accepting new request - %s\n", strerr(server.connections[conn_id]));
					continue; /* continue with a new request */
				}
				printf("Serving %s::%d on socket %d\n", inet_ntoa(server.connections[conn_id].d_ip),
																							ntohs(server.connections[conn_id].port), server.connections[conn_id].socket);
					quit = 0; /* sets the initial value for the flag */
	        while(!quit)
	        {
	            result = serve_request( &(server.connections[conn_id]), fname);
	            switch(result)
	            {
	                case -1:
	                    printf("Error - %s\n", strerr(server.connections[conn_id]));
	                    result = send_err_msg( &(server.connections[conn_id]) );
	                    if(result < 0)
	                        printf("Error - %s\n", strerr(server.connections[conn_id]));
	                    server_close_conn(&server, conn_id);
											quit = 1;
	                    break;
	                case 0: /* GET request */
	                    result = fsend( &(server.connections[conn_id]), fname);
	                    if(result < 0)
	                    { /* error message already sent */
	                        printf("Connection closed with the client\n");
	                        server_close_conn(&server, conn_id);
	                        quit = 1;
	                    }
	                    break;
	                case 1: /* 	QUIT message*/
	                    printf("Client %s::%d on socket %d closed... releasing resources\n", inet_ntoa(server.connections[conn_id].d_ip),
	                                                    ntohs(server.connections[conn_id].port), server.connections[conn_id].socket);
	                    server_close_conn(&server, conn_id);
	                    quit = 1; /* flag sets */
	                    break;
	            }
	        }
	        printf("\n---------------\n");
    }

	return 0;
}

/* return -1 in case of error. Then the connection must be closed */
int fsend(conn_t* conn, char* fname)
{
	int result;
	/* else starts to send the file */
	result = transmit_file(conn, fname);
	if(result < 0)
	{
		printf("Error - %s\n", strerr(*conn));
		send_err_msg(conn);	/* it sends only in the case of still allocated connection */
		return -1;
	}
	else
		printf("File '%s' trasmitted to the client successfully\n", fname);
	return 0;
}
