/*
 * Main purpose of the program:
 * 
 * The client's functions 
 * - process stdin (user keyboard input) and then send to server
 * --- handle client side's internal cmds
 * - process msgs from socket(server, in this case) 
 * 
 * So basically, the client can "recv msg from server" and "send 
 * msg to server" any time and in any combination.
 *
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 

#define STDIN 0 // File descriptor for standard input

void sendToServer(char buf[], int sockfd);

int main(int argc, char *argv[])
{ 
/*  // Address format
    struct sockaddr_in {
      sa_family_t    sin_family; // address family: AF_INET 
      in_port_t      sin_port;   // port in network byte order 
      struct in_addr sin_addr;   // internet address 
    };

    // Internet address. 
    struct in_addr {
      uint32_t       s_addr; // address in network byte order
    };  
*/  struct sockaddr_in serv_addr;

/*  struct servent {
      char  *s_name;       // official service name 
      char **s_aliases;    // alias list 
      int    s_port;       // port number 
      char  *s_proto;      // protocol to use
    } 
*/  struct servent* se;

    int sockfd=0, sts=0, errsv=0;
    int maxfd=0; //max num of file descriptors
    char buf[1024];
    fd_set rfds;

#if defined(PRINT_UDPAPI_COMMANDS) || defined(PRINT_SOCKET_DEBUG)
    time_t	ttime;
    struct tm 	*sttm;
#endif

    //stdin_i = fileno(stdin); // Get stdin's fd for select()

    if(argc != 3){
        printf("\n Usage: %s server_ip server_port\n",argv[0]);
        return 1;
    } 

    // socket(): Linux socket interface
    // AF_INET: IPv4 Internet protocol
    // SOCK_STREAM: TCP packets
    // SOCK_DGRAM : UDP packets
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        printf("\n Error : Could not create socket \n");
        return 1;
    }
    maxfd = sockfd;

    memset(&serv_addr, '0', sizeof(serv_addr)); 
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2])); 

    // inet_pton(): convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr)<=0){
        printf("\n inet_pton error occured\n");
        return 1;
    } 
    
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
       printf("\n Error : Connect Failed \n");
       return 1;
    } 

    printf("Enter a message or type \"quit\" to exit.\n\n");

    // Listen for messages coming from server or characters from stdin
    do {
    	memset(buf, '\0', sizeof(buf));	

      FD_ZERO(&rfds); //initialize an empty file descriptor set
      FD_SET(STDIN, &rfds); //add stdin to the set
      FD_SET(sockfd, &rfds); //add sockct to the set

/* Wait until "stdin" or "socket" has input */
	    sts = select(maxfd+1, &rfds, NULL, NULL, NULL); // selecting
	    if (sts < 0) {		
		    //EINTR
    		//- this error occurs when a call is interrupted. So the current 
    		//  call doesn't succeed.  But it might work when the call is 
		    //  called again.
    		//  - "Interrupted" here means a signal from the system level
    		//  interrupt the current system call
		
		    // If the value of errno should be preserved across a library 
    		// call, it must be saved:
		    errsv = errno;
 
    		if (errsv != EINTR) {
		      printf("select() failed: %s\n", strerror(errsv));
   		    break;
		    } 
		    else if (errsv == EBADF) {
		      printf("select() failed: %s\n", strerror(errsv));
		      break;
		    } 	
	    } // End of 1st if

/* Process stdin(user's keyboard input) and send it to server */
    	if ( FD_ISSET(STDIN, &rfds) ) {
	      // Read the user's keyboard input
    	  sts = read(STDIN, buf, sizeof(buf));
    	  if (sts >= 0) {
  	      // Process "q" cmd to exit the client
	        if (strcmp(buf,"quit\n") == 0) {
	          printf("Client closed connection\n");
		        break;
	        }
	        // Process stdin: "atugpib ......." cmd and send to server
	        else if ( strncmp(buf, "atugpib", 7) == 0 ) {
        		// Close everything, ie. Server, Client, GIB connections, etc.
        		if ( strncmp(buf, "atugpib exit", 12) == 0 ) {	
        		  sendToServer(buf, sockfd); // Server handles it
        		  break; // Close client connection
		        }
	        }		
          // Process regular msg and send to server
	        else {
		        sendToServer(buf, sockfd);
	        }
	      } 
	      else {
          perror("ERROR reading from STDIN\n");
	        exit(1);
	      }
	    } // End of 2nd if

/* Process socket input(msg reveiced from server) */
	    if ( FD_ISSET(sockfd, &rfds) ) { 			
		    // Read msg from server
		    sts = read(sockfd, buf, sizeof(buf));
		    if (sts < 0) {
          perror("ERROR reading from socket\n");
	        exit(1);
	    	}
	    	else if (sts == 0) { 
		      printf("Client closed connection\n");
		      break;
	    	}	
		    else {
 	        printf("Rcvd:%s\n\n",buf);
		    }
      } // End of 3rd if
	
    } while (1);

    // Remove from fd set 
    FD_CLR(STDIN, &rfds);
    FD_CLR(sockfd, &rfds);

    return 0;
}

void sendToServer(char buf[], int sockfd) 
{
	int sts=0;

	printf("Sent: %s\n", buf);
			
	// Send the input to server
	sts = write(sockfd, buf, strlen(buf));
	if (sts < 0) 
	{
	  perror("ERROR writing to socket\n");
          exit(1);
	}	
}
