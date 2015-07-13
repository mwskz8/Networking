#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "5777"   // port we're listening on

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
  fd_set master;    // master file descriptor list
  fd_set read_fds;  // temp file descriptor list for select()
  
  int fdmax;        // maximum file descriptor number
  int listener;     // listening socket descriptor
  int newfd;        // newly accept()ed socket descriptor

  struct sockaddr_storage remoteaddr; // client address
  char remoteIP[INET6_ADDRSTRLEN];
  
  // socklen_t: an unsigned opaque int type of length of at least 32 bits
  socklen_t addrlen;

  //struct addrinfo: network address and service translation 
  struct addrinfo hints, *ai, *p;
  
  char buf[256];    // buffer for client data
  
  int yes=1;        // for setsockopt() SO_REUSEADDR, below
  int nbytes, i, j, rv;


  FD_ZERO(&master);    // clear the master and temp sets
  FD_ZERO(&read_fds);

  // get us a socket and bind it
  memset(&hints, 0, sizeof hints);

  /*AF_UNSPEC: indicates that getaddrinfo()  should return  socket  
     addresses  for  any  address family (ie. IPv4 or IPv6) that can
     be used with node and service. -- from getaddrinfo() below. */
  hints.ai_family = AF_UNSPEC;

  hints.ai_socktype = SOCK_STREAM;

  /*AI_PASSIVE flag: 
    -If it's specified in hints.ai_flags, and node is NULL, then
     the returned socket addresses will be suitable for  bind(2)ing  a  socket  that
     will  accept(2)  connections.   The  returned  socket  address will contain the
     "wildcard address" (INADDR_ANY for IPv4 addresses,  IN6ADDR_ANY_INIT  for  IPv6
     address).   The  wildcard  address  is used by applications (typically servers)
     that intend to accept connections on any of the hosts's network addresses.   
    -If node is not NULL, then the AI_PASSIVE flag is ignored. */
  hints.ai_flags = AI_PASSIVE;

  /*int getaddrinfo(const char *node, const char *service,
                     const struct addrinfo *hints, 
                     struct addrinfo **res); //res: result
    
    - this func is related to network address and service translation. getaddrinfo()
    returns one or more addrinfo structures, each of which contains an Internet 
    address that can be specified in a call to bind(2) or connect(2).

    node    : Internet host
    service : Internet service
    hints   :-Pts to an addrinfo struct specifying criteria for selecting
               the socket addr struct returned in the list pted to by 
               res(last parameter of getaddrinfo()).
             -If hints is not NULL it  points  to  an  addrinfo  structure  
               whose  ai_family, ai_socktype,  and  ai_protocol  specify  
               criteria  that limit the set of socket addresses returned 
               by getaddrinfo(). */
  if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
    fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
    exit(1);
  }
    
  for(p = ai; p != NULL; p = p->ai_next) {
    listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    printf("---fd of listener: %d\n", listener);
    if (listener < 0) { 
      continue;
    }
        
    // lose the pesky "address already in use" error message
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
      close(listener);
      continue;
    }
    break;
  }

  // if we got here, it means we didn't get bound
  if (p == NULL) {
    fprintf(stderr, "selectserver: failed to bind\n");
    exit(2);
  }

  // frees the memory that was allocated for the dynamically allocated 
  // linked list res. 
  freeaddrinfo(ai); // all done with this

  // listen
  if (listen(listener, 10) == -1) {
    perror("listen");
    exit(3);
  }

  // add the listener to the master set
  FD_SET(listener, &master);

  // keep track of the biggest file descriptor
  fdmax = listener; // so far, it's this one

  for(;;) { // main loop

    /* Reason to have 2 fd_sets: master and read_fds
       - select() changes the set passed to it to reflect which sockets are
       ready to read. To keep track of the connections from one call of 
       select() to the next, we must store these safely. 
       - At the last minute, copy the master into the read_fds, and then
       call select(). */
    read_fds = master; // copy it
              
    if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
      perror("select");
      exit(4);
    }

    for(i = 0; i <= fdmax; i++) { // looping through fd's

      if (FD_ISSET(i, &read_fds)) { // incoming?


        if (i == listener) { /* handle new connections */
          printf("------Handle new connection...\n");
          addrlen = sizeof remoteaddr;

          /* Check to see when the listener socket is ready to read. When it 
             is, it means I have a new connection pending, and I accept() it 
             and add it to the master set. */
          newfd = accept(listener,(struct sockaddr *)&remoteaddr,&addrlen);
          printf("---accept() ");

          if (newfd == -1) {
            perror("accept");
          }
          else {
            FD_SET(newfd, &master); // add to master set
            if (newfd > fdmax) {    // keep track of the max
              fdmax = newfd;
              printf("---fdmax: %d\n", fdmax);
            }
            printf("selectserver: new connection from %s on socket %d\n",
                    inet_ntop(remoteaddr.ss_family, 
                              get_in_addr((struct sockaddr*)&remoteaddr), 
                              remoteIP, 
                              INET6_ADDRSTRLEN),
                    newfd);
          }
        } 
        else { /* handle data from a client */
          printf("------Handle data from a client...\n");
          /* When a client connection is ready to read
             - if recv() returns 0, the client has closed the connection, and 
               I must remove it from the master set.  
             - if recv() returns non-zero, some data has been received. Then 
               go through the master list and send the data to all the rest of
               the connected clients. */
          if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
            printf("---Connection closed by client, or got error\n");
            if (nbytes == 0) { // connection closed
              printf("selectserver: socket %d hung up\n", i);
            } 
            else { //got error
              perror("recv");
            }
            close(i); // bye!
            FD_CLR(i, &master); // remove from master set
          } 
          else { // we got some data from a client
            printf("---Rcved data from client\n");
            for(j = 0; j <= fdmax; j++) { // send to everyone!
              // except the listener and the client that sent the msg
              if (j != listener && j != i) {
                if (j != listener) {
                  if (send(j, buf, nbytes, 0) == -1) {
                    perror("send");
                  }
                }
              }
            }
          }
        } // END handle data from client
            

      } // END got new incoming connection

    } // END looping through fd's

  } // END main look for(;;)
    
    return 0;
}
