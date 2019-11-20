#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <string.h>


//#ifdef WIN32
#include <windows.h>
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>


//SOCKET fd;

//#else /* WIN32 */
//#include <unistd.h>
//#include <sys/types.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>

//#define INVALID_SOCKET -1
//#define SOCKET_ERROR -1
//int fd;

//#endif /* WIN32 */

//#define DEFAULT_PORT "1100"
#define DEFAULT_PORT "4000"
#define DEFAULT_BUFLEN 4096

#define THREADREADY 0
#define THREADRUNNING 1
#define THREADSTOP 2

#define MAXCONNECTION 16

#define USE_SELECT

#define ParentProxyHost "localhost"
#define ParentProxyPort "8080"


SOCKET makeListenSocket(LPSTR);
SOCKET makeConnectSocket(char* HOSTNAME , LPSTR);
DWORD WINAPI waitRecieveThread(LPVOID);


typedef struct{
  HANDLE* hThread;
  SOCKET csocket;
  char   status;
  int    sockid;
} sockMng ;



int main(void){
  
  int iResult , nResult;
  SOCKET ListenSocket = INVALID_SOCKET , ClientSocket = INVALID_SOCKET;

  HANDLE hThread ;
  DWORD dwThreadID ;

  HANDLE hThreadList[MAXCONNECTION];
  sockMng sockmng[MAXCONNECTION];
  SOCKADDR_STORAGE sockAddr;
  int AddrLen;

  WSADATA wsaData;
  
  fd_set fdRead;
  struct timeval timeout;

  timeout.tv_sec = 50;
  timeout.tv_usec = 500*1000 ;
  

  /* Initinalize hThreadList/sockmng */
  for(int i=0;i<MAXCONNECTION;i++){
    hThreadList[i] = NULL;
    sockmng[i].hThread = NULL;
    sockmng[i].csocket = INVALID_SOCKET;
    sockmng[i].status = THREADREADY ;
    sockmng[i].sockid = i;
  }

  
  iResult = WSAStartup(MAKEWORD(2,2), &wsaData);  /* Winsock ver 2.2 */
  if (iResult != 0) {
    printf("WSAStartup failed: %d\n", iResult);
    return 1;
  }

  // Initialize Winsock
  ListenSocket = makeListenSocket(DEFAULT_PORT); /* getaddrinfo , socket(CreateSocket) , bind , listen . if fail return -1 */
  if( iResult == INVALID_SOCKET ){
    printf("makeListenSocket fail: %d\n", WSAGetLastError());
    return 1;
  }
  
  while(1){

#ifdef USE_SELECT
    
    FD_ZERO(&fdRead);
    FD_SET(ListenSocket, &fdRead);

    for(int i=0;i<MAXCONNECTION;i++){
      if( sockmng[i].csocket != INVALID_SOCKET )
	FD_SET(sockmng[i].csocket , &fdRead );
    }

    nResult = select(0, &fdRead, NULL, NULL, &timeout);

    printf("nResult=%d\n",nResult);
    /*
    if( nResult == SOCKET_ERROR ){
      printf("select failed: %d\n", WSAGetLastError());
      closesocket(ListenSocket);
      WSACleanup();
      return 1;
    }else if( nResult == 0 ){
      printf("continue..\n");
      continue;
    } // else ...
    */
    
    AddrLen = sizeof(SOCKADDR_STORAGE);
    ClientSocket = accept(ListenSocket, (LPSOCKADDR)&sockAddr, &AddrLen);
    // getnameinfo( (LPSOCKADDR)&sockAddr, AddrLen, (char*)HostName, sizeof(HostName), NULL, 0, 0);

    if (ClientSocket == INVALID_SOCKET) {
      printf("accept failed: %d\n", WSAGetLastError());
      closesocket(ListenSocket);
      WSACleanup();
      return 1;
    }
      
    iResult = -1;
    for(int i=0; i<MAXCONNECTION; i++){
      if( sockmng[i].status == THREADREADY ){ /* THREADREADYなら CreateThread する。みつからないなら失敗した となって作らない */
	printf("sockid:%d\n",i);
	sockmng[i].status = THREADRUNNING;
	sockmng[i].csocket = ClientSocket ;
	sockmng[i].hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)waitRecieveThread, &(sockmng[i]), 0, &dwThreadID); /* ひきすうは自IDなど含んだ構造体にする事。終了ステータスを返すため */
	iResult = i;
	break;
      }
    }
    
    if( iResult == -1 ){
      printf("can not make Thread: %d\n",WSAGetLastError());
      CloseHandle(sockmng[iResult].hThread);
      closesocket(sockmng[iResult].csocket);
      
      sockmng[iResult].status = THREADREADY;
      sockmng[iResult].hThread = NULL ;
      sockmng[iResult].csocket = INVALID_SOCKET ;
      /* return iResult;  /* 作れなかった、と記録するだけで動きつづけるため、main() では return しない */
    }
  }

  for(int i=0; i<MAXCONNECTION; i++){   
    CloseHandle(sockmng[i].hThread);
  }
    
#else  // not USE_SELECT
  // Accept a client socket
  ClientSocket = accept(ListenSocket, NULL, NULL);
  if (ClientSocket == INVALID_SOCKET) {
    printf("accept failed: %d\n", WSAGetLastError());
    closesocket(ListenSocket);
    WSACleanup();
    return 1;
  }
  
  iResult = -1;
  for(int i=0; i<MAXCONNECTION; i++){
    if( sockmng[i].status == THREADREADY ){ /* THREADREADYなら CreateThread する。みつからないなら失敗した となって作らない */
      printf("sockid:%d\n",i);
      sockmng[i].status = THREADRUNNING;
      sockmng[i].csocket = ClientSocket ;
      sockmng[i].hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)waitRecieveThread, &(sockmng[i]), 0, &dwThreadID); /* ひきすうは自IDなど含んだ構造体にする事。終了ステータスを返すため */
      iResult = i;
      break;
    }
  }
  
  if( iResult == -1 ){
    printf("can not make Thread");
    CloseHandle(sockmng[iResult].hThread);
    closesocket(sockmng[iResult].csocket);
    
    sockmng[iResult].status = THREADREADY;
    sockmng[iResult].hThread = NULL ;
    sockmng[iResult].csocket = INVALID_SOCKET ;
    /* return iResult;  /* 作れなかった、と記録するだけで動きつづけるため、main() では return しない */
  }
  

#endif

  WSACleanup();
  return 0;
  
}



/* 受信スレッドルーティン */
DWORD WINAPI waitRecieveThread(LPVOID sockmng){ //lpClientSocket -> sockmng 修正
  /* LPVOID型 引数のarglistとして渡される。
  よびだし側は (HANDLE)hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)waitReceiveThread, &(sockmng[i]), 0, &dwThreadID);
  */
  char recvbuf[DEFAULT_BUFLEN];
  int iSendResult , iResult ;
  int recvbuflen = DEFAULT_BUFLEN;

  int sockid = ((sockMng *)sockmng)->sockid; 
  SOCKET ClientSocket = ((sockMng *)sockmng)->csocket; /* sockmng を sockMng * にCASTして、そのcsocketを取り出し ClientSocket へ入れる */
  SOCKET ConnectSocket ;

  /* proxyへのソケットを生成する */
  ConnectSocket = makeConnectSocket(ParentProxyHost , ParentProxyPort);
  if( ConnectSocket == INVALID_SOCKET ){
    /* 生成に失敗したらこのスレッドは消える */
    printf("makeConnectSocket fail: %d\n", WSAGetLastError());
    closesocket(ConnectSocket);
    closesocket(ClientSocket);
    ((sockMng *)sockmng)->status = THREADSTOP ;
    ExitThread(TRUE);
  }

  /* ループ */
  do {

    /* Client から受信 */
    iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
    
    if (iResult == 0){
      /* Clientからのコネクションがクローズしちゃった→セッション終了 */
      printf("sockid:%d/Connection closing...\n",sockid);
      int iiResult = shutdown(ClientSocket, SD_BOTH);
      if (iiResult == SOCKET_ERROR) {
	printf("sockid:%d/shutdown failed: %d\n", sockid, WSAGetLastError());
	closesocket(ClientSocket);
	closesocket(ConnectSocket);
	break ;
	
      }else if (iResult < 0) {
	/* Client からの受信で失敗 → コネクションが異常とみなしセッション終了 */
	printf("sockid:%d/recv failed: %d\n", sockid, WSAGetLastError());
	closesocket(ClientSocket);
	closesocket(ConnectSocket);
	break;

      } else if(iResult > 0) {
	
#ifdef DEBUG
	printf("sockid:%d/Bytes received: %d\n", sockid, iResult);
#endif // DEBUG

	/* Client からの受信データがあった→Connectサーバに送信する */
	iSendResult = send(ConnectSocket , recvbuf , iResult, 0);

#ifdef DEBUG      
	printf("sockid:%d/Bytes sent(to ParentServer): %d\n", sockid, iSendResult);
#endif //DEBUG

	if (iSendResult == SOCKET_ERROR) {
	  /* Connectサーバへの送信に失敗した→セッション終了 */
	  printf("sockid:%d/send failed: %d\n", sockid, WSAGetLastError());
	  closesocket(ClientSocket);
	  closesocket(ConnectSocket);
	  return 1; // スレッドのエラーの戻り値は 1 でいいのか?
	}

      // cleanup
      printf("sockid:%d/Close socket\n",sockid);
      closesocket(ConnectSocket);
      closesocket(ClientSocket);

    }else {

    
  } while (iResult > 0);

  ((sockMng *)sockmng)->status = THREADSTOP ;
  
  ExitThread(TRUE);
  // 戻り値を書く必要がある? 
}





/* socket を生成、bindし、listen するまで */
/* 引数はポート番号(文字リテラルでポートを記載 ex. "1100" ) LPSTR とはそういうものらしい */
SOCKET makeListenSocket(LPSTR localportnum){

  WSADATA wsaData;
  int iResult;
  ADDRINFO hints;
  LPADDRINFO result;
  SOCKET retSocket = INVALID_SOCKET;

  ZeroMemory(&hints, sizeof(ADDRINFO));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags    = AI_PASSIVE;
  
  // Resolve the local address and port to be used by the server
  iResult = getaddrinfo(NULL, localportnum, &hints, &result);
  if (iResult != 0) {
    printf("getaddrinfo failed: %d\n", iResult);
    WSACleanup();
    return INVALID_SOCKET ;
  }
  
  
  // Create a SOCKET for the server to listen for client connections
  retSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  
  if (retSocket == INVALID_SOCKET) {
    printf("Error at socket(): %ld\n", WSAGetLastError());
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }
  
  // Setup the TCP listening socket(BIND)
  iResult = bind( retSocket, result->ai_addr, (int)result->ai_addrlen);
  if (iResult == SOCKET_ERROR) {
    printf("bind failed with error: %d\n", WSAGetLastError());
    closesocket(retSocket);
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }
  
  
  // Setup the TCP listening socket(listen)
  //if ( listen( retSocket, SOMAXCONN ) == SOCKET_ERROR ) {
  if ( listen( retSocket, 15 ) == SOCKET_ERROR ) {
    printf( "Listen failed with error: %ld\n", WSAGetLastError() );
    closesocket(retSocket);
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }

  // free 'reslut'(getaddrinfo's result)
  freeaddrinfo(result);

  
  return retSocket ;
  
}

/* socket を生成、bindし、connect するまで */
/* 引数はポート番号(文字リテラルでポートを記載 ex. "1100" ) LPSTR とはそういうものらしい */
SOCKET makeConnectSocket(char* hostname, LPSTR localportnum){

  WSADATA wsaData;
  int iResult;
  ADDRINFO hints;
  LPADDRINFO result;
  SOCKET retSocket = INVALID_SOCKET;

  ZeroMemory(&hints, sizeof(ADDRINFO));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags    = AI_PASSIVE;
  
  // Resolve the local address and port to be used by the server
  iResult = getaddrinfo(hostname, localportnum, &hints, &result);
  if (iResult != 0) {
    printf("getaddrinfo failed: %d\n", iResult);
    WSACleanup();
    return INVALID_SOCKET ;
  }
  
  
  // Create a SOCKET for the client to connect for server connections
  retSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  
  if (retSocket == INVALID_SOCKET) {
    printf("Error at make socket(): %ld\n", WSAGetLastError());
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }
  
  // Setup the TCP connect(connect)
  iResult = connect( retSocket, result->ai_addr, (int)result->ai_addrlen);
  if (iResult == SOCKET_ERROR) {
    printf("connect failed with error: %d\n", WSAGetLastError());
    closesocket(retSocket);
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }
  
  // free 'reslut'(getaddrinfo's result)
  freeaddrinfo(result);

  return retSocket ;

}

