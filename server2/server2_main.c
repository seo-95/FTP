#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include "../errlib.h"
#include "../FTP_lib.h"

//#define DEBUG
#define MAX_ACTIVE_PROC 500

char *prog_name;
server_t server;
int conn_id;
int currprocn;

int fsend(conn_t* conn, char* fname);

void sig_manager(int sig)
{
	int count,
		  status;
	pid_t p;
	if(sig == SIGINT)
	{
		 /* deallocates all the common structure. If some files are open during the SIGNINT catching,
		    it's not possible to close it */
			shut_down(&server);
			printf(" [%d] -- Server shutted down\n", getpid());
			exit(0);
	}
	if(sig == SIGCHLD)
	{
		while( (p = waitpid(-1, &status, WNOHANG)) > 0)
		{
			currprocn --;
			if(WIFEXITED(status))
				printf("[%df] -- Process %d terminated with status %d\n", getpid(), p, WEXITSTATUS(status));
		}
	}
	if(sig == SIGPIPE)
	{
		server_close_conn(&server, conn_id);
		printf("Connection closed because of BROKEN PIPE\n");
	}
}


int main(int argc, char *argv[])
{

	int result,
		  quit;
	char fname[MAX_FILE_NAME];
	currprocn = 1; //father only

	signal(SIGINT, sig_manager);
	signal(SIGCHLD, sig_manager);

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
		printf("Error - %s", server.error);
		shut_down(&server);
		exit(-1);
	}
	else
		if(result == -2)
		{
			printf("Error - %s\n", strerr(server.connections[0]));
			shut_down(&server);
			err_quit("Exiting . . .");
		}
	printf("[%df] -- Server listening on port %s\n", getpid(), argv[1]);
    while(1)
    {
        memset(fname, '\0', MAX_FILE_NAME * sizeof(char));
				result = accept_new_request(&server, &conn_id);
				if(result < 0)
				{
					printf("Error accepting new request - %s\n", strerr(server.connections[conn_id]));
					continue; /* continue with a new request */
				}
				while(currprocn > MAX_ACTIVE_PROC)
				{
					printf("Cannot serve request for now. Too many processes running\n");
					pause(); //wait for a signal (SIGCHLD)
				}

				result = fork();
				if(result == -1)
				{
					printf("%s\n", strerror(errno));
					server_close_conn(&server, conn_id);
					continue;
				}
				if(result != 0) /* father comes back to listen for new request (to the accept() ) */
				{
					currprocn ++;
					server_close_conn(&server, conn_id); /* cleans the child resources */
					continue;
				}

				/* new child process: */
				printf("[Process %d] -- Serving %s::%d on socket %d\n", getpid(), inet_ntoa(server.connections[conn_id].d_ip),
																							ntohs(server.connections[conn_id].port), server.connections[conn_id].socket);
					quit = 0; /* sets the initial value for the flag */
	        while(!quit)
	        {
	            result = serve_request( &(server.connections[conn_id]), fname);
#ifdef DEBUG
							sleep(10);
#endif
	            switch(result)
	            {
	                case -1:
	                    printf("[Process %d] -- Error - %s\n", getpid(), server.connections[conn_id].err_msg);
	                    result = send_err_msg( &(server.connections[conn_id]) );
	                    if(result < 0)
	                        printf("[Process %d] -- Error - %s\n", getpid(), strerr(server.connections[conn_id]));
	                    server_close_conn(&server, conn_id);
											quit = 1;
	                    break;
	                case 0: /* GET request */
	                    result = fsend( &(server.connections[conn_id]), fname);
	                    if(result < 0)
	                    { /* error message already sent */
													printf("[Process %d] -- Client %s::%d on socket %d closed... releasing resources\n", getpid(),
																													inet_ntoa(server.connections[conn_id].d_ip),
			                                                    ntohs(server.connections[conn_id].port), server.connections[conn_id].socket);
	                        server_close_conn(&server, conn_id);
													quit = 1;
	                    }
	                    break;
	                case 1: /* 	QUIT message*/
	                    printf("[Process %d] -- Client %s::%d on socket %d closes the connection... releasing resources\n", getpid(),
																											inet_ntoa(server.connections[conn_id].d_ip),
	                                                    ntohs(server.connections[conn_id].port), server.connections[conn_id].socket);
	                    server_close_conn(&server, conn_id);
	                    quit = 1; /* flag sets */
	                    break;
	            }
	        }
					printf("[Process %d] -- END\n", getpid());
					shut_down(&server); //shut down server structure for the child
					exit(0); /* end of process */
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
		printf("[Process %d] -- Error - %s\n", getpid(), strerr(*conn));
		send_err_msg(conn);		/* it sends only in the case of still allocated connection */
		return -1;
	}
	else
		printf("[Process %d] -- File '%s' trasmitted to the client successfully\n", getpid(), fname);
	return 0;
}
