

/* STATEFUL SERVER IMPLEMENTATION FOR A SIMPLE FTP CLIENT-SERVER APPLICATION */

                /* @author: Matteo Senese */


#ifndef FTPLIB_H

#define FTPLIB_H
#define MAX_FILE_NAME 30  /* the max size of the file name you can request */
#define GET_SIZE (6 + MAX_FILE_NAME)  /* for GET <filename><CR><LF> */

#include <sys/file.h> // for file lock
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <errno.h>
#include <netinet/in.h>
#include "../sockwrap.h"

/**
  * This is the struct that identifies the connection to a specific host
  * on the network
  */
typedef struct CONN_HANDLER
{
        uint8_t active;     /* flag that tells if the connections is active or not (1 or 0) */
        struct in_addr s_ip;  /* source ip (in the network notation) */
        struct in_addr d_ip;	/* dest ip (in the network notation) */
        uint16_t port;  /* dest port (in the network notation) */
        int socket;     /* connection socket id */
        char *r_buf;  	/* buffer for receiving message */
        char *s_buf;	/* buffer for sending message */
        char *err_msg;  /* buffer containing error msg */
} conn_t;

/**
 * @brief It returns the correspondant error message handling possible premature
 *        deallocations due to host disconnection
 * @param conn
 * @return It return the string message of the error if available, otherwise simply
 * the information about the host disconnection
 */

char* strerr(conn_t connection);


/*                           *************************************
                             *               CLIENT              *
                             *************************************                   */


/**
 * @brief It established the connection with an ip and a specified port
 * @param conn
 * @param ip
 * @param port
 * @return It return -1 in case of mem error or -2 in case of connection problems
 *      (more details about the error are provided through the err_msg into the conn_t structure)
 *      (before call it remember to close the previous conn_t)
 */
int connect_to(conn_t *connection, char *ip, char *port);

/**
 * @brief requests and receives a file from the connected server inside
 * @param connection
 * @param filenme
 * @param dim wil be filled this function
 * @param timestamp will be filled by this function
 * @return -1 in case of error
 */
int receive_file(conn_t* conn, char* filename, uint32_t* dim, uint32_t* timestamp);

/**
  * @brief sends the quit message to the server for connection closing
  * @param connection
  * @return -1 in case of error
  */
int send_quit_msg(conn_t* conn);

/**
 * @brief closes the connection and remove all the associated data
 * @param conn
 */
void close_conn(conn_t *conn);



/*                           *************************************
                             *               SERVER              *
                             *************************************                   */

#define MAX_REQUEST_N 1 /* maximum # of request the server could handle
                          In practice the real number of connections will be
                          MAX_REQUEST_N + 1 because the server needs the first one
                          to be listening only */
#define MAX_PENDING_REQUEST 10
#define TIMEOUT_SEC 90


/**
  * This structure is used for handlw the server and manage
  * its status and active connections
  */
typedef struct SERVER_HANDLE
{
  uint8_t stat_flag; /* to be accessed by the strstat() function
                      * possible stat:
                      * UP          (first bit = 1)
                      * DOWN        (first bit = 0)
                      * IN_ERR      (second bit = 1)
                      * ... other bits for future improvements ...
                      */
  conn_t* connections; /* array of active connections for that server
                        * the first connection (connections[0]) is for
                        * listening operation only (it is connected with no host).
                        * This array structure is useful to keep the information about
                        * the different clients in a multi-thread environment.
                        * Future improvements : in order to make this server able to work
                        * with several hundred of clients, it is better to increase the efficiency
                        * by replacing this array with a more efficient structure for data retrieval
                        * (e.g. hash base strucure, tree-map ... )
                        */
  uint8_t connections_n; /* #of active connections for that server */
  uint16_t list_port; /* port on which the server is listening on */
  char* error; /* error message for the server setted in case of IN_ERR status */
} server_t;

/**
  * @brief It turns on the server
  * @param server_t
  * @return -1 in case of sever memory error or -2 in case of secondary mem ERROR
  *         (more details about the error are provided through the error field in server_t)
  */
int server_setup(server_t* server);

/**
  * @brief  It creates the first connection (connections[0]) and
  *         set it to listen on a specified port
  *         after this call the d_ip field will be void (all 0's)
  *         (before call it remember to shut_down the previous allocated server_t)
  * @param server
  * @param port
  * @return It returns -1 in case of server error or -2 in case of connection problems
  *         (more details about the error are provided through the err_msg into the conn_t structure)
  */
int listen_on(server_t* server, char* port);

/**
  * @brief It accepts a new request for that server and fill the conn_id
  *         with the id for that connection. In case of new connected host
  *         it creates a new one.
  * @param server
  * @param conn_id
  * @return -1 in case of error
  *         (more details about the error are provided through the err_msg into conn_t structure)
  */
int accept_new_request(server_t* server, int* conn_id);

/**
  * @brief It serves the request on a particular connection.
  *         In case of GET it fills also the fname
  * @param conn
  * @param fname
  * @return -1 in case of error (e.g. out of protocol message)
  *          0 in case of GET request
  *          1 in case of QUIT request
  */
int serve_request(conn_t* conn, char* fname);

/**
  * @brief It sends an -ERR\r\n message on that connection
  * @param conn
  * @return -1 in case of uncomplete message sent
  */
int send_err_msg(conn_t* conn);

/**
  * @brief It sends the file on that connection
  * @param conn
  * @param filename
  * @return -1 in case of error
  */
int transmit_file(conn_t* conn, char* filename);

/**
  * @brief It returns the status of that server
  * @param server
  * @return 0 in case of DOWN
  *         1 in case of UP
  *         2 in case of ERR
  */
uint8_t intstat(server_t server);

/**
  * @brief It closes the connection identified by conn_id.
  *        The valid conn_id goes from 1 to MAX_REQUEST_N,
  *        if conn_id == 0 does nothing
  * @param server
  * @param conn_id
  */
void server_close_conn(server_t* server, int conn_id);

/**
  * @brief It shutdown the server and release all the associated connections
  * @param server
  */
void shut_down(server_t* server);


#endif //FTPLIB_H
