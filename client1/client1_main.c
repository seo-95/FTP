#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "../errlib.h"
#include "../FTP_lib.h"


#define DIR_LENGTH 90



char *prog_name;
conn_t connection;

void args_download(int filenum, char **fnames);
int fdownload(char* fname);

void sig_manager(int sig)
{
  if(sig == SIGINT)
  {
    close_conn(&connection);
    exit(0);
  }

  if(sig == SIGPIPE)
  {
    close_conn(&connection);
    printf("Connection closed because of BROKEN PIPE\n");
    exit(0);
  }
}



/* argv[1] -> ip server address
   argv[2] -> port#
   argv[3 .. m] -> files name
*/
int main (int argc, char *argv[])
{
    int result;

    if(argc < 4) /* prog name + 2 args + (files name . . . ) */
        err_quit("Wrong parameter");
    prog_name = argv[0];

    signal(SIGPIPE, sig_manager);
    signal(SIGINT, sig_manager);
    /* CONNECTS TO THE SERVER */
    result = connect_to(&connection, argv[1], argv[2]);
    switch(result)
    {
    case -1:
        close_conn(&connection);
        err_quit("Memory error");
        break;
    case -2:
        printf("Error - %s\n", strerr(connection));
        close_conn(&connection); //deallocates all the resources
        exit(-1);
        break;
    }

    /* download the file(s) passed by arguments */
    args_download(argc, argv);

    printf("/*------*/\n");
    /* closes the connection */
    result = send_quit_msg(&connection);
    if(result < 0)
    {
      close_conn(&connection);
      err_quit("Error: %s\n", strerr(connection));
    }


    printf("Quit message sent successfully\n");
    close_conn(&connection);
    printf("Connection closed\n");
    return 0;
}



/* do-while body because it is needed at least once (argc must be at least 4) */
void args_download(int filenum, char **filenames)
{
    /* COMMUNICATE WITH THE SERVER */
    int filecount = 3; /* the first file name is inside argv[3] */
    do
    {
        printf("\n---------------\n");
        /* FILE DOWNLOADING */
        if(fdownload(filenames[filecount]) < 0 )
        {
          printf("Connection closed by the server\n");
          break;
        }
        /* next file request */
        filecount ++;
    }
    while(filecount < filenum);

}


int fdownload(char* fname)
{
    char cwd[DIR_LENGTH];
    uint32_t fdim, ftime;
    int result;
    printf("Downloading file: %s . . .\n", fname);
    /* FILE DOWNLOADING */
    result = receive_file(&connection, fname, &fdim, &ftime);
    /* case of general error during the downloading */
    if(result < 0)
    {
        printf("Error in downloading file %s - (%s)\n", fname, strerr(connection));
        if(remove(fname) == -1) /* remove the corrupted file */
                printf("Unable to delete the corrupted file %s - %s\n", fname, strerror(errno));
        else
                printf("Corrupted file successfully removed\n");

        if(result == -2) /* -ERR message, connection closed by the server */
          return -1;
    }
    /* else everything went well */
    else
    {
        /* prints the file path */
        if(getcwd(cwd, sizeof(cwd)) != NULL)
            printf("File %s correctly stored in %s\nDimension: %u\nTimestamp: %u\n", fname, cwd, fdim, ftime);
        else
            printf("Something went wrong - (%s)\n", strerror(errno));
    }
}
