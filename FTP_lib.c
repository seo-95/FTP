
#include "FTP_lib.h"
#include <sys/select.h>
#include <sys/times.h>

#define BUFLEN 1024
#define FILE_CHUNK 1000 /* how many bytes reading once */


//#define DEBUGMODE



/*                           *************************************
                             *               CLIENT              *
                             *************************************                   */


static int reads_firstb(conn_t* conn);
static int reads_ok(conn_t* conn);
static int reads_dim(conn_t* conn, uint32_t* dim);
static int reads_timestamp(conn_t* conn, uint32_t* time);
static int wait_for_event(int sock);

typedef struct TIMEOUT_HANDLER
{
  fd_set fds; /* in this case we need only 1 type of events */
  struct timeval timer;
} select_t;

static int setup(conn_t* conn)
{
    conn->r_buf = NULL;
    conn->s_buf = NULL;
    conn->err_msg = NULL;
    memset( &(conn->s_ip), 0, sizeof(conn->s_ip));
    memset( &(conn->d_ip), 0, sizeof(conn->d_ip));
    memset( &(conn->socket), 0, sizeof(conn->socket));
    memset( &(conn->port), 0, sizeof(conn->port));
    memset( &(conn->active), 0, sizeof(conn->port));

    /* initialization of the err_msg */
    if(  (conn->err_msg = (char*) malloc(BUFLEN*sizeof(char))) == NULL )
        return -1;
    /* initialization for both r_buf and s_buf */
    if( (conn->r_buf = (char*) malloc(BUFLEN*sizeof(char)) ) == NULL )
        return -1;
    if( (conn->s_buf = (char*) malloc(BUFLEN*sizeof(char)) ) == NULL )
        return -1;
#ifdef DEBUGMODE
    printf("Everything allocated successfully\n");
#endif
    conn->active = 1; /* activates that connection */
    memset(conn->s_buf, '\0', BUFLEN);
    memset(conn->r_buf, '\0', BUFLEN);
    memset(conn->err_msg, '\0', BUFLEN);
    return 0;
}


/**
 * connection function
 */
int connect_to(conn_t* conn, char* ip, char* port)
{
    uint16_t portnum;
    int result;
    struct in_addr ipaddr;
    struct sockaddr_in saddr;

    /* initialize the memory for the connection data */
    result = setup(conn);
    if(result == -1) /* checking for mem error */
    {
        /* delete all the possible allocating data */
        close_conn(conn);
        return -1;
    }

    /* initialization for the socket */
    conn->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(conn->socket < 0)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Socket error", BUFLEN-1);
        return -2;
    }

    /* processing the ip address string */
    result = inet_aton(ip, &ipaddr); /* result used only for debug purpose */
    conn->s_ip = ipaddr; /* the source is always local host for this version */
    result = inet_aton(ip, &ipaddr);
    if(result == 0)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Invalid IP address", BUFLEN-1);
        return -2;
    }
    conn->d_ip = ipaddr; /*  in the network notation */

    /* processing the port string */
    if (sscanf(port, "%" SCNu16, &portnum)!=1)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Invalid port number", BUFLEN-1);
        return -2;
    }
    conn->port = htons(portnum); /* in network notation */

    /* prepares the address structure */
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port   = conn->port;
    saddr.sin_addr   = ipaddr;

    /* connect */
#ifdef DEBUGMODE
    showAddr("Connecting to target address", &saddr);
#endif
    result = connect(conn->socket, (struct sockaddr*) &saddr, sizeof(saddr));
    if(result < 0)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Connection error", BUFLEN-1);
        return -2;
    }
#ifdef DEBUGMODE
    printf("done.\n");
#endif

    return 0;
}


/**
 * sends a get message to the connected host
 */
static int get_file(conn_t *conn, char* filename)
{
    int wrtb;

    memset(conn->err_msg, '\0', BUFLEN);
    memset(conn->s_buf, '\0', BUFLEN);
    /* composes the GET message for request */
    sprintf(conn->s_buf, "GET %s\r\n", filename);
#ifdef DEBUGMODE
    printf("Request message was ok : %s\n", conn->s_buf);
#endif
    wrtb = writen(conn->socket, conn->s_buf, strlen(conn->s_buf));
    if(wrtb < 0)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Connection - error while sending GET request", BUFLEN-1);
        return -1;
    }
#ifdef DEBUGMODE
    printf("GET sent successfully\n");
#endif
    return 0;
}



int receive_file(conn_t* conn, char* filename, uint32_t* dim, uint32_t* timestamp)
{
    int recvb,
        result,
        leftb;
    FILE* desc;
    memset(conn->err_msg, '\0', BUFLEN);
    memset(conn->r_buf, '\0', BUFLEN);
    /* it sends the get message for that file */
    result = get_file(conn, filename);
    if(result < 0)
    	return -1;
    /*reads the first byte (+ or -) */
    if(wait_for_event(conn->socket) == 0)
    {
      if(conn->err_msg != NULL)
        strncpy(conn->err_msg, "timeout from client", BUFLEN-1);
      return -1;
    }
    result = reads_firstb(conn);  /* no select here beacause I have to wait for precedent client requests in case of sequential server */
    if(result == -1) /* -ERR or out of protocol msg */
    {
        recvb = read(conn->socket, conn->r_buf, 5); /* not readn because it may be an out of protocol message (so it can be blocked) */
        if(strcmp(conn->r_buf, "ERR\r\n") == 0 && conn->err_msg != NULL)
            strncpy(conn->err_msg, "Error: -ERR message received!", BUFLEN-1);
        else if(conn->err_msg != NULL)
            strncpy(conn->err_msg, "Protocol - unknown message from server", BUFLEN-1);
        return -2;
    }
    /* +OK message */
    result = reads_ok(conn);
    if(result == -1)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Connection - error while receiving", BUFLEN-1);
        return -1;
    }
    result = reads_dim(conn, dim);
    if(result == -1)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Error - invalid dimension", BUFLEN-1);
        return -1;
    }
    result = reads_timestamp(conn, timestamp);
    if(result == -1)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Error - invalid timestamp", BUFLEN-1);
        return -1;
    }

    desc = fopen(filename, "wb");
    if(desc == NULL)
    {
      if(conn->err_msg != NULL)
        strncpy(conn->err_msg, "Error - invalid timestamp", BUFLEN-1);
      return -1;
    }
    result = flock(fileno(desc), LOCK_EX); //acquire the lock on that file
    if(result < 0)
    {
      fclose(desc);
      if(conn->err_msg != NULL)
        strncpy(conn->err_msg, strerror(errno), BUFLEN-1);
      return -1;
    }
    leftb = *dim;
    if(*dim == 0)
    {
      flock(fileno(desc), LOCK_UN);
      fclose(desc);
      return 0; /* does not write anything on the file and returns */
    }
    recvb = -1; //initialization
    do
    {
        if(recvb == 0) //no more data on the socket but file still miss
        {
          flock(fileno(desc), LOCK_UN);
          fclose(desc);
          if(conn->err_msg != NULL)
            strncpy(conn->err_msg, "Connection - error while receiving", BUFLEN-1);
          return -1;
        }
        memset(conn->r_buf, '\0', BUFLEN);
        if(conn->r_buf == NULL) /* in case of closed and deallocated connection */
        {
          flock(fileno(desc), LOCK_UN);
          fclose(desc);
          return -1;
        }
        recvb = read(conn->socket, conn->r_buf, FILE_CHUNK);
        if(recvb < 0 || strcmp(conn->r_buf, "-ERR\r\n") == 0)
        {
            flock(fileno(desc), LOCK_UN);
            fclose(desc);
            if(conn->err_msg != NULL)
              strncpy(conn->err_msg, "Connection - error while receiving", BUFLEN-1);
            return -1;
        }

#ifdef DEBUGMODE
    printf("%s\n", conn->r_buf);
#endif
    if(recvb != 0)
        fwrite(conn->r_buf, 1, recvb, desc);

    leftb -= recvb;
    } while(leftb > 0);

   flock(fileno(desc), LOCK_UN);
   fclose(desc);
   return 0;
}



static int reads_firstb(conn_t* conn)
{
    memset(conn->r_buf, '\0', BUFLEN);
    int readb = read(conn->socket, conn->r_buf, 1);
#ifdef DEBUGMODE
    printf("%s", conn->r_buf);
#endif
    if(strcmp(conn->r_buf, "+") == 0)
        return 0;
    /* also in the case of out of protocol message */
    return -1;
}



static int reads_ok(conn_t* conn)
{
    memset(conn->r_buf, '\0', BUFLEN);
    read(conn->socket, conn->r_buf, 4);
    if(strcmp(conn->r_buf, "OK\r\n") == 0)
        return 0;
    /* also in the case of out of protocol message */
    return -1;
}



static int reads_dim(conn_t* conn, uint32_t* dim)
{
    uint32_t size;
    memset(conn->r_buf, '\0', BUFLEN);
    int readb = readn(conn->socket, &size, 4); /* readn because it cannot be less */
    if(readb < 4)
        return -1;
    *dim = ntohl(size); /* from network byte order to host one */
#ifdef DEBUGMODE
    printf("dim: %u\n", *dim);
#endif
    return 0;
}


static int reads_timestamp(conn_t* conn, uint32_t* time)
{
    uint32_t ts;
    memset(conn->r_buf, '\0', BUFLEN);
    int readb = readn(conn->socket, &ts, 4);
    if(readb < 4)
        return -1;
    *time = ntohl(ts); /* from network byte order to host one */
#ifdef DEBUGMODE
    printf("ts: %u\n", *time);
#endif
        return 0;
}



/* Sends message for closing the connection */
int send_quit_msg(conn_t* conn)
{
    int wrtb;

    if(conn->s_buf == NULL) //deallocated structure
      return -1;
    memset(conn->s_buf, '\0', BUFLEN);
    sprintf(conn->s_buf, "%s", "QUIT\r\n");
#ifdef DEBUGMODE
    printf("QUIT message was ok : %s\n", conn->s_buf);
#endif

    wrtb = writen(conn->socket, conn->s_buf, 6);
    if(wrtb < 0)
    {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Connection - error while sending QUIT request", BUFLEN-1);
        return -1;
    }
#ifdef DEBUGMODE
    printf("QUIT sent successfully\n");
#endif
    return 0;
}



/**
 * closes the connection
 */
void close_conn(conn_t* conn)
{
    close(conn->socket);
    free(conn->r_buf);
    free(conn->s_buf);
    free(conn->err_msg);
    conn->r_buf = NULL;
    conn->s_buf = NULL;
    conn->err_msg = NULL;
#ifdef DEBUGMODE
    printf("Deallocation ok\n");
#endif
}


char* strerr(conn_t connection)
{
  if(connection.err_msg == NULL)
    return "Host disconnected";
  else
    return connection.err_msg;
}


/*                           *************************************
                             *               SERVER              *
                             *************************************                   */

static int open_connection(server_t* server, struct in_addr dest, uint16_t cl_port, int sock, int* conn_id);
static int exists_conn(struct in_addr addr, uint16_t cl_port, conn_t* conns, int conn_n, int* conn_id);
static int is_off(server_t server);
static int is_in_err(server_t server);
static int search_available(server_t server);
static int is_path_allowed(const char* s);

int server_setup(server_t* server)
{
  memset( &(server->stat_flag), 0, sizeof(server->stat_flag)); /* sets DOWN */
  server->connections = NULL;
  server->error = NULL;
  memset( &(server->connections_n), 0, sizeof(server->connections_n));

  server->error = (char*) malloc(BUFLEN * sizeof(char));
  if(server->error == NULL)
  {
    server->stat_flag = server->stat_flag ^ 2; /* sets ERR */
    return -1;
  }
  /* MAX_REQUEST_N + 1 because the first one is listen only */
  server->connections = (conn_t*) malloc( (MAX_REQUEST_N+1) * sizeof(conn_t));
  if(server->connections == NULL)
  {
    server->stat_flag = server->stat_flag ^ 2; /* sets ERR */
    strncpy(server->error, "Unable to handle so many requests. Unsufficient memory", BUFLEN-1);
    return -2;
  }

  server->stat_flag = server->stat_flag ^ 1; /* sets UP */
  memset(server->error, '\0', BUFLEN);
  memset(server->connections, 0, (MAX_REQUEST_N + 1) * sizeof(conn_t));
  return 0;
}


/* listen on that port. It creates the first connection*/
int listen_on(server_t *server, char* port)
{
  conn_t* conn = &(server->connections[0]);
  uint16_t portnum;
  int result;
  struct in_addr ipaddr;
  struct sockaddr_in my_addr;

  if(is_off(*server))
  {
    strncpy(server->error, "The server is off. Please turn it on", BUFLEN-1);
    return -1;
  }
  if(is_in_err(*server))
  {
    strncpy(server->error, "The server is in error", BUFLEN-1);
    return -1;
  }

  /* initialize the memory for the listening conn */
  result = setup(conn);
  if(result == -1)
  {
    /* delete all the possible allocating data */
    close_conn(conn);
    strncpy(server->error, "Not enough memory", BUFLEN-1);
    return -1;
  }
  if(server->connections_n < 1)
    server->connections_n ++; /* at least one connections (the listening one) */

  /* initialization for the socket */
  conn->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(conn->socket < 0)
  {
      if(conn->err_msg != NULL)
        strncpy(conn->err_msg, "Socket - error", BUFLEN-1);
      return -2;
  }

  /* processing the port string */
  if (sscanf(port, "%" SCNu16, &portnum)!=1)
  {
      if(conn->err_msg != NULL)
        strncpy(conn->err_msg, "Invalid port number", BUFLEN-1);
      return -2;
  }
  server->list_port = htons(portnum); /* in network notation */

  /* For the server we know only the source ip actually
  *  (the dest ip will be available after the accept())
  * result used only for debug purpose */
  result = inet_aton("0.0.0.0", &ipaddr);
  conn->s_ip = ipaddr;

  /* bind the socket to localhost only */
  bzero(&my_addr, sizeof(my_addr));
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = server->list_port;
  my_addr.sin_addr.s_addr = INADDR_ANY;
#ifdef DEBUGMODE
  showAddr("Binding to address", &my_addr);
#endif
  result = bind(conn->socket, (struct sockaddr *) &my_addr, sizeof(my_addr));
  if(result < 0)
  {
    if(conn->err_msg != NULL)
      strncpy(conn->err_msg, strerror(errno), (BUFLEN-1) * sizeof(char));
    return -1;
  }
#ifdef DEBUGMODE
  printf("done.\n");
#endif
  /* starts to listen on that socket and define the max # of pending requests on that socket queue */
  result = listen(conn->socket, MAX_PENDING_REQUEST);
  if(result < 0)
  {
    if(conn->err_msg != NULL)
      strncpy(conn->err_msg, strerror(errno), BUFLEN-1);
    return -1;
  }

  return 0;
}

/* done by the first conn_t ( connections[0] )*/
int accept_new_request(server_t* server, int* conn_id)
{
  int result,
      new_sock;
  conn_t* listening_conn = &(server->connections[0]);
  struct sockaddr_in client_addr;
  socklen_t client_l;

  bzero(&client_addr, sizeof(client_addr));
  client_l = sizeof(struct sockaddr_in);

  /* accept returns the new socket copy of the previous one */
  new_sock = accept(listening_conn->socket, (struct sockaddr*) &client_addr, &client_l);
  if(new_sock < 0)
  {
    if(listening_conn->err_msg != NULL)
    {
      strncpy(listening_conn->err_msg, strerror(errno), BUFLEN-1);
      strncat(listening_conn->err_msg, " (accept() error)", BUFLEN-1);
    }
    *conn_id = 0; /* listening connection */
    return -1;
  }
  /* searching for an already existing connection with that host */
  if(exists_conn(client_addr.sin_addr, client_addr.sin_port, server->connections, server->connections_n, conn_id))
  {
    server->connections[*conn_id].socket = new_sock; /* update the socket */
    return 0; /* conn_id already setted by previous function */
  }

  else
  {
    if(server->connections_n < (MAX_REQUEST_N + 1) ) /* the first one is listening only */
    {
      result = open_connection(server, client_addr.sin_addr, client_addr.sin_port, new_sock, conn_id);
      if(result < 0)
      {
        if(listening_conn->err_msg != NULL)
          strncpy(listening_conn->err_msg, "Memory problem", BUFLEN-1);
        *conn_id = 0;
        return -1;
      }

      return 0;
    }
    else
    {
      if(listening_conn->err_msg != NULL)
        strncpy(listening_conn->err_msg, "Maximum #of requests reached. Impossible to handle another one", BUFLEN-1);
      return -1;
    }
  }
}


int serve_request(conn_t* conn, char* fname)
{
  int readb;
  uint32_t result;
  memset(conn->err_msg, '\0', BUFLEN);
  memset(conn->r_buf, '\0', BUFLEN);
  memset(fname, '\0', MAX_FILE_NAME);

  readb = read(conn->socket, conn->r_buf, 4);  /* read is better for detecting out of protocol message */
  if(readb == -1)
  {
    if(conn->err_msg != NULL)
      strncpy(conn->err_msg, strerror(errno), BUFLEN-1);
    return -1;
  }
  if(readb < 4)
  {
    if(conn->err_msg != NULL)
      strncpy(conn->err_msg, "unknown message", BUFLEN-1);
    return -1;
  }
#ifdef DEBUGMODE
      printf("%s", conn->r_buf);
#endif
  if( strcmp(conn->r_buf, "GET ") == 0)
  {
    memset(conn->r_buf, '\0', BUFLEN); /* cleaning */
    if(wait_for_event(conn->socket) == 0)
    {
      if(conn->err_msg != NULL)
        strncpy(conn->err_msg, "timeout from server", BUFLEN-1);
      return -1;
    }
    readb = read(conn->socket, conn->r_buf, MAX_FILE_NAME + 2); /* read because I don't know the exact filename length */
#ifdef DEBUGMODE
    printf("%s", conn->r_buf);
#endif
    if(conn->r_buf[readb - 2] == '\r' && conn->r_buf[readb - 1] == '\n')
    {   /*presence of QUIT after GET due to client timeout*/
      if(readb >= 8 && conn->r_buf[readb-7] == '\n' && conn->r_buf[readb-8] == '\r')
      {
          if(conn->err_msg != NULL)
            strncpy(conn->err_msg, "QUIT message received", BUFLEN-1);
          return 1;  /* returns the QUIT*/
      }
      strncpy(fname, conn->r_buf, readb - 2);
      if(is_path_allowed(fname))
        return 0;
      else
      {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "Path out of server file system", BUFLEN-1);
        return -1;
      }
    }
    else
    {
      if(conn->err_msg != NULL)
        strncpy(conn->err_msg, "unknown message", BUFLEN-1);
      return -1;
    }
  }

  else
    if( strcmp(conn->r_buf, "QUIT") == 0)
    {
      memset(conn->r_buf, '\0', BUFLEN); /* cleaning */
      readb = readn(conn->socket, conn->r_buf, 2); /* reads \r\n */
      if(readb != 2 || strcmp(conn->r_buf, "\r\n") != 0)
      {
        if(conn->err_msg != NULL)
          strncpy(conn->err_msg, "unknown message", BUFLEN-1);
        return -1;
      }
      return 1; /* QUIT message */
    }
  else
    return -1;
}


static int is_path_allowed(const char* s)
{
  if(!s || (s[0] == '~' || s[0] == '/'))
       return 0;
   if(strstr(s, ".."))
      return 0;
   return 1;
}

int send_err_msg(conn_t* conn)
{
  int recvb;
  if(conn->s_buf == NULL) //deallocated structure
    return -1;
  memset(conn->s_buf, '\0', BUFLEN);
  strncpy(conn->s_buf, "-ERR\r\n", BUFLEN-1);
  recvb = writen(conn->socket, conn->s_buf, strlen(conn->s_buf));
  if(recvb <  6)
  {
    if(conn->err_msg != NULL)
      strncpy(conn->err_msg, "Uncomplete message was sent", BUFLEN-1);
    return -1;
  }
  return 0;
}


int transmit_file(conn_t* conn, char* filename)
{
  int recvb,
      timestamp,
      readb,
      result;
  FILE* fptr;
  uint32_t size;
  struct stat st;

  fptr = fopen(filename, "rb");
  if(fptr == NULL)
	{
    if(conn->err_msg != NULL)
		  strncpy(conn->err_msg, "Unable to locate the file", BUFLEN-1);
		return -1;
	}
  result = flock(fileno(fptr), LOCK_EX);
  if(result < 0)
  {
    fclose(fptr);
    fptr = NULL;
    if(conn->err_msg != NULL)
		  strncpy(conn->err_msg, strerror(errno), BUFLEN-1);
		return -1;
  }
  memset(conn->s_buf, '\0', BUFLEN);
  // writes +OK\r\n
  strncpy(conn->s_buf, "+OK\r\n", BUFLEN-1);
  recvb = writen(conn->socket, conn->s_buf, strlen(conn->s_buf));

  // writes dimension on 4 bytes
  stat(filename, &st);
  size = (uint32_t) st.st_size; //off_t not well defined. Not easy to check if 32bits are enough
  size = htonl(size);
  recvb = writen(conn->socket, &size, 4);
  if(recvb == -1)
  {
    flock(fileno(fptr), LOCK_UN);
    fclose(fptr);
    fptr = NULL;
    if(conn->err_msg != NULL)
      strncpy(conn->err_msg, "Problem when sending", BUFLEN-1);
    return -1;
  }
  //writes timestamp on 4 bytes
  timestamp = (int) st.st_mtime;
  timestamp = htonl(timestamp);
  recvb = writen(conn->socket, &timestamp, 4);
  if(recvb == -1)
  {
    flock(fileno(fptr), LOCK_UN);
    fclose(fptr);
    fptr = NULL;
    if(conn->err_msg != NULL)
      strncpy(conn->err_msg, "Problem when sending", BUFLEN-1);
    return -1;
  }
  //sends the file
  size_t sentdim = 0;
  size_t readdim = 0;
  while(1)
  {
    memset(conn->s_buf, '\0', BUFLEN);
    readb = fread(conn->s_buf, 1, FILE_CHUNK, fptr);
    if(readb == 0)
      break; /* EOF */
    recvb = writen(conn->socket, conn->s_buf, readb * sizeof(char));
    if(recvb < readb)
    {
      flock(fileno(fptr), LOCK_UN);
      fclose(fptr);
      fptr = NULL;
      if(conn->err_msg != NULL)
        strncpy(conn->err_msg, "File transmit error", BUFLEN-1);
      return -1;
    }
    readdim += readb;
    sentdim += recvb;
  }
  flock(fileno(fptr), LOCK_UN);
  fclose(fptr);
	fptr = NULL;
  return 0;
}


/* open a new connection with a specified host */
static int open_connection(server_t* server, struct in_addr dest, uint16_t cl_port, int sock, int* conn_id)
{
  int result;
  /* searches for the first available connection spot */
  *conn_id = search_available(*server);
  /* setup memory for buffers */

  result = setup( &(server->connections[*conn_id]) ) ;
  server->connections_n ++;
  if(result < 0)
  {
    /* delete all the possible allocating data */
    close_conn( &(server->connections[*conn_id]) );
    return -1;
  }
  server->connections[*conn_id].s_ip = server->connections[0].s_ip;
  server->connections[*conn_id].d_ip = dest;
  server->connections[*conn_id].port =  cl_port;
  server->connections[*conn_id].socket = sock;
  return 0;
}

static int search_available(server_t server)
{
    int i;
    /* do not consider the listening conn[0] */
    for(i = 1; server.connections[i].active == 0 && i < MAX_REQUEST_N; i++);
    return i;
}

/* it searches for a connection matching address addr and sets
 *correspondingly the conn_id */
static int exists_conn(struct in_addr addr, uint16_t cl_port, conn_t* conns, int conn_n, int* conn_id)
{
  int count;
  /* the first connection is listen only so count starts from 1 */
  for(count = 1; count < conn_n; count ++)
  {   /* conns[count].d_ip.s_addr can be 0 if this connection position was closed */
    if(conns[count].d_ip.s_addr != 0 && addr.s_addr == conns[count].d_ip.s_addr && conns[count].port == cl_port)
    {
      *conn_id = count;
      return 1;
    }
  }
  return 0; /* not found */
}


/* returns 1 if server is down */
static int is_off(server_t server)
{
  return !(server.stat_flag & 1);
}


/* returns 1 if server is in error */
static int is_in_err(server_t server)
{
  return (server.stat_flag & 2);
}


/* returns the actual state of the server */
uint8_t intstat(server_t server)
{
  if( (server.stat_flag ^ 2) >= 2) /* in ERR */
    return 2;
  else
    return (server.stat_flag ^ 1); /* ON or OFF */
}


int wait_for_event(int sock)
{
  select_t time_wrapper;
  FD_ZERO(&time_wrapper.fds);
  FD_SET(sock, &time_wrapper.fds);
  time_wrapper.timer.tv_sec = TIMEOUT_SEC;
  time_wrapper.timer.tv_usec = 0;
  return select(sock+1, &time_wrapper.fds, NULL, NULL, &time_wrapper.timer);
}

/* it does not deallocate the conn_id instance, it simply clear that position because of reusing
  another possible approach is to use a list for connections (inefficient for connections retrieving) */
void server_close_conn(server_t* server, int conn_id)
{
  /* the user is not allowed to close the listening connection
   * or to close something already closed, otherwise the #connections
   * will be wrong
   */
  if(conn_id == 0 || (conn_id+1) > server->connections_n)
    return;
  close_conn( &(server->connections[conn_id])); /* it performs the free and sets to null */
  server->connections_n --;
}


void shut_down(server_t* server)
{
  int count;
  for(count = 0; count < server->connections_n; count ++)
    close_conn(&(server->connections[count]));
  free(server->connections);
  server->connections_n = 0;
  free(server->error);
  /* shutdown the server */
  memset( &(server->stat_flag), 0, sizeof(server->stat_flag));
  #ifdef DEBUGMODE
      printf("Server shutting down was ok\n");
  #endif
  server->connections = NULL;
  server->error = NULL;
}
