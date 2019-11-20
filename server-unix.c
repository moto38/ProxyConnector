#undef UNICODE

#define WIN32_LEAN_AND_MEAN


#include <stdio.h>
#include <string.h>


#ifdef WIN32
#include <windows.h>
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>

SOCKET fd;

#else /* WIN32 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
int fd;

#endif /* WIN32 */

#define DEFAULT_PORT "1100"
#define DEFAULT_BUFLEN 4096


int main(void){
  
#ifdef WIN32

  WSADATA wsaData;
  int iResult;

  // Initialize Winsock
  iResult = WSAStartup(MAKEWORD(2,2), &wsaData);  /* Winsock ver 2.2 */
  if (iResult != 0) {
    printf("WSAStartup failed: %d\n", iResult);
    return 1;
  }

  
  struct addrinfo *result = NULL, *ptr = NULL, hints;

  ZeroMemory(&hints, sizeof (hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;
  
  // Resolve the local address and port to be used by the server
  iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
  if (iResult != 0) {
    printf("getaddrinfo failed: %d\n", iResult);
    WSACleanup();
    return 1;
  }

  
  SOCKET ListenSocket = INVALID_SOCKET;
  
  
  // Create a SOCKET for the server to listen for client connections
  
  ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  
  if (ListenSocket == INVALID_SOCKET) {
    printf("Error at socket(): %ld\n", WSAGetLastError());
    freeaddrinfo(result);
    WSACleanup();
    return 1;
  }
  
  // Setup the TCP listening socket(BIND)
  iResult = bind( ListenSocket, result->ai_addr, (int)result->ai_addrlen);
  if (iResult == SOCKET_ERROR) {
    printf("bind failed with error: %d\n", WSAGetLastError());
    freeaddrinfo(result);
    closesocket(ListenSocket);
    WSACleanup();
    return 1;
  }
  
  // free 'reslut'(getaddrinfo's result)
  freeaddrinfo(result);
  
  // Setup the TCP listening socket(listen)
  if ( listen( ListenSocket, SOMAXCONN ) == SOCKET_ERROR ) {
    printf( "Listen failed with error: %ld\n", WSAGetLastError() );
    closesocket(ListenSocket);
    WSACleanup();
    return 1;
  }
  
#else /* WIN32 */

  /* make server socket */
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if( sock == -1 ){
    perror("socket");
    return 1;
  }

  /* make struct sockaddr_in */
  struct sockaddr_in sa = {0};
  sa.sin_family = AF_INET;
  sa.sin_port   = htons(DEFAULT_PORT);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);

  
  /* bind */
  if( bind(sock, (struct sockaddr*) &sa, sizeof(struct sockaddr_in)) == -1 ){
    perror("bind");
    goto bail;
  }

  
  /* listen */
  if( listen(sock, 128) == -1){
    perror("listen");
    goto bail;
  }
  
#endif /* WIN32 */


#ifdef WIN32
  
  SOCKET ClientSocket;
  ClientSocket = INVALID_SOCKET;
  
  // Accept a client socket
  ClientSocket = accept(ListenSocket, NULL, NULL);
  if (ClientSocket == INVALID_SOCKET) {
    printf("accept failed: %d\n", WSAGetLastError());
    closesocket(ListenSocket);
    WSACleanup();
    return 1;
  }
    
    
  char recvbuf[DEFAULT_BUFLEN];
  //int iResult, iSendResult; 
  int iSendResult;
  int recvbuflen = DEFAULT_BUFLEN;
    
  // Receive until the peer shuts down the connection
  do {
    
    iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
    if (iResult > 0) {
      printf("Bytes received: %d\n", iResult);
      
      // Echo the buffer back to the sender
      iSendResult = send(ClientSocket, recvbuf, iResult, 0);
      if (iSendResult == SOCKET_ERROR) {
	printf("send failed: %d\n", WSAGetLastError());
	closesocket(ClientSocket);
	WSACleanup();
	return 1;
      }
      printf("Bytes sent: %d\n", iSendResult);

    } else if (iResult == 0){
      printf("Connection closing...\n");
    }else {
      printf("recv failed: %d\n", WSAGetLastError());
      closesocket(ClientSocket);
      WSACleanup();
      return 1;
    }
    
  } while (iResult > 0);


  // shutdown the send half of the connection since no more data will be sent
  iResult = shutdown(ClientSocket, SD_SEND);
  if (iResult == SOCKET_ERROR) {
    printf("shutdown failed: %d\n", WSAGetLastError());
    closesocket(ClientSocket);
    WSACleanup();
    return 1;
  }

  // cleanup
  closesocket(ClientSocket);
  WSACleanup();
  
  return 0;
  
}
  
#else /* WIN32 */

  while(1){
    /* wait client connect */
    int fd = accept(sock, NULL, NULL); // sock, clientaddr , addlen(clientaddr)
    if( fd == -1 ){
      perror("accept");
      goto bail;
    }


    /* receve */
    char buffer[DEFAULT_BUFLEN];
    int recv_size = read(fd, buffer, sizeof(buffer)-1 );
    if( recv_size == -1 ){
      perror("read");
      close(fd);
      goto bail;
    }
    
    /* print receive data */
    buffer[recv_size] = '\0';
    printf("message: %s\n", buffer);

    /* close socket */
    if( close(fd) == -1 ){
      perror("close");
      goto bail;
    }

  }

 bail:
  /* error processing */
  close(sock);
  return 1;

}
    
#endif /* WIN32 */
    

