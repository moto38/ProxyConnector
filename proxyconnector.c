#undef UNICODE


#include <stdio.h>
#include <string.h>


//#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")

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
//#define DEFAULT_PORT "18080"
#define DEFAULT_BUFLEN 4096

#define THREADREADY 0
#define THREADRUNNING 1
#define THREADSTOP 2

//#define MAXCONNECTION 256
#define MAXCONNECTION 2

#define USE_SELECT

#define ParentProxyHost "localhost"
#define ParentProxyPort "8080"  //"14096" //


SOCKET makeListenSocket(LPSTR);
SOCKET makeConnectSocket(char* HOSTNAME , LPSTR);
DWORD WINAPI subthreadmgr(LPVOID);
DWORD WINAPI communicationThread(LPVOID);
int senddata(int sockid, SOCKET sockfrom , char *buf , int buflen , SOCKET sockto);
int recvdata(int sockid, SOCKET sockfrom , char *buf , int buflen , SOCKET sockto);
char *convbuf(char*);

typedef struct{
  HANDLE* hThread;
  HANDLE* cThread; /* ClientSocket reciever */
  HANDLE* sThread; /* ConnectScoket reciever */
  SOCKET csocket; /* ClientSocket */
  SOCKET ssocket; /* ConnectSocket */
  char   status;
  int    sockid;
} sockMng ;


typedef struct{
  HANDLE* hThread;
  SOCKET* csocket; /* ClientSocket */
  SOCKET* ssocket; /* ConnectSocket */
  char* status;
  int* sockid;
} subsock ;


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
  
  fd_set fds, readfds;
  struct timeval timeout;

  DWORD dwNonBlocking = 1;

  timeout.tv_sec = 5;
  timeout.tv_usec = 1000*500;

  

  /* Initinalize hThreadList/sockmng */
  for(int i=0;i<MAXCONNECTION;i++){
    hThreadList[i] = NULL;
    sockmng[i].hThread = NULL;
    sockmng[i].cThread = NULL;
    sockmng[i].sThread = NULL;
    sockmng[i].csocket = INVALID_SOCKET;
    sockmng[i].ssocket = INVALID_SOCKET;
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
  if( ListenSocket == INVALID_SOCKET || ListenSocket == SOCKET_ERROR ){
    printf("makeListenSocket fail: %d\n", WSAGetLastError());
    return 1;
  }else{
#ifdef DEBUG
    printf("makeListenSocket succeed ListenSocket: %d\n", ListenSocket);
#endif
    ;
  }

  while(1){
    /* socket connection come */
    AddrLen = sizeof(SOCKADDR_STORAGE);
    //AddrLen = sizeof(sockaddr_in);
    ClientSocket = accept(ListenSocket, (LPSOCKADDR)&sockAddr, &AddrLen);
    // getnameinfo( (LPSOCKADDR)&sockAddr, AddrLen, (char*)HostName, sizeof(HostName), NULL, 0, 0);
    
    if (ClientSocket == INVALID_SOCKET || ClientSocket == SOCKET_ERROR ) {
      printf("accept failed: %d\n", WSAGetLastError());
      closesocket(ListenSocket);
      WSACleanup();
      return 1; // or continue; ?
    }
    
    iResult = -1;
    for(int i=0; i<MAXCONNECTION; i++){
      if( sockmng[i].status == THREADREADY ){ /* THREADREADYなら CreateThread し、iResult=i。ループが最後までいった場合は「thread生成に失敗した」ということでiResult=-1のまま */
	
#ifdef DEBUG	
	printf("sockid:%d\n",i);
#endif //DEBUG       
	sockmng[i].status = THREADRUNNING;
	sockmng[i].csocket = ClientSocket ;
	sockmng[i].hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)subthreadmgr, &(sockmng[i]), 0, &dwThreadID); /* ひきすうは自IDなど含んだ構造体にする事。終了ステータスを返すため */
	iResult = i;
	break;
      }
    }
    
    if( iResult == -1 ){
      printf("can not make Thread: %d\n",WSAGetLastError());
    }
  }
  
  for(int i=0; i<MAXCONNECTION; i++){   
    CloseHandle(sockmng[i].hThread);
  }
  
  WSACleanup();
  return 0;
}

DWORD WINAPI communicationThread(LPVOID sockmng){
  /* LPVOID型 引数のarglistとして渡される。
  よびだし側は (HANDLE){c|s}hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)communicationThread, &(subsock), 0, &dwThreadID);
  */
  char recvbuf[DEFAULT_BUFLEN] ;
  
  int iResult ;
  int recvbuflen = DEFAULT_BUFLEN ;
  
  int sockid = *(((subsock *)sockmng)->sockid); 
  SOCKET fromSocket = *(((subsock *)sockmng)->csocket) ;
  SOCKET toSocket = *(((subsock *)sockmng)->ssocket) ;
  
  int nResult;

  memcpy(recvbuf , "\0" , 1);

  /* ループ */
  do{
    
    iResult = recvdata(sockid, fromSocket, recvbuf, recvbuflen, toSocket);
	
    /* iResult がエラーやセッションクローズだったら do ループを break してスレッドを終了してmainに戻る */
    /* >0 データがあるのなら読み取り send する */
    /* -1 データがないのならまたループ */
    if( iResult < 0 ){
      break;
    }else{
      recvbuflen = iResult ;
#ifdef DEBUG
      printf("SockID:%d has data from socket:%d, invoke senddata(). DATA=%s\n",sockid, fromSocket, recvbuf);
#endif
      
      iResult = senddata(sockid, toSocket, recvbuf, recvbuflen, fromSocket);
#ifdef DEBUG
      printf("SockID:%d/senddata to toSocket:%d return:%d. DATA=%d\n",sockid, toSocket, iResult, recvbuf);
#endif //DEBUG
      if( iResult < 0 ){
	break;
      }else{
	recvbuf[0] = '\0';
	recvbuflen = 0;
	continue;
      }
    }

    if( *(((subsock *)sockmng)->status) == THREADSTOP ){
      break;
    }

  }while(TRUE);

#ifdef DEBUG
  printf("SockID:%d thread stop.\n",sockid);
#endif
    
  *(((subsock *)sockmng)->status) = THREADSTOP ;
  
  ExitThread(TRUE);
}




/* 受信スレッドマネージャー */
DWORD WINAPI subthreadmgr(LPVOID sockmng){ //lpClientSocket -> sockmng 修正
  /* LPVOID型 引数のarglistとして渡される。
  よびだし側は (HANDLE)hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)subthreadmgr, &(sockmng[i]), 0, &dwThreadID);
  */

  int iResult, nResult;
  HANDLE chThread, shThread ;

  int sockid = ((sockMng *)sockmng)->sockid; 
  SOCKET ClientSocket ;
  SOCKET ConnectSocket ;

  DWORD cdwThreadID , sdwThreadID;

  subsock csubsock , ssubsock;
  
  
  /* Client/proxyへのソケット */
  ClientSocket = ((sockMng *)sockmng)->csocket; /* sockmng を sockMng * にCASTして、そのcsocketを取り出し ClientSocket へ入れる */
  ConnectSocket = makeConnectSocket(ParentProxyHost , ParentProxyPort);

  if( ConnectSocket == INVALID_SOCKET || ConnectSocket == SOCKET_ERROR ){
    /* 生成に失敗したらこのスレッドは消える */
    printf("makeConnectSocket fail: %d\n", WSAGetLastError());
    closesocket(ConnectSocket);
    closesocket(ClientSocket);
    ((sockMng *)sockmng)->status = THREADSTOP ;
    ExitThread(TRUE);
  }

#ifdef DEBUG
  printf("SockId:%d ClientSocket:%d/ConnectSocket:%d\n",sockid, ClientSocket , ConnectSocket);
#endif

  /* ClientScoket 受信用スレッドの生成 */
  csubsock.sockid = &(((sockMng *)sockmng)->sockid);
  csubsock.csocket = &(ClientSocket);
  csubsock.ssocket = &(ConnectSocket);
  csubsock.status = &(((sockMng *)sockmng)->status);
  chThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)communicationThread, &(csubsock), 0, &cdwThreadID);
#ifdef DEBUG
  printf("Sockid:%d ClientSocket thread:%d\n", sockid, cdwThreadID);
#endif
  
  /* ConnectSocket 受信用スレッドの生成 */
  ssubsock.sockid = &(((sockMng *)sockmng)->sockid);
  ssubsock.csocket = &(ConnectSocket);
  ssubsock.ssocket = &(ClientSocket);
  ssubsock.status = &(((sockMng *)sockmng)->status);
  shThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)communicationThread, &(ssubsock), 0, &sdwThreadID);
#ifdef DEBUG
  printf("Sockid:%d ConnectSocket thread:%d\n", sockid, sdwThreadID);
#endif

  /* ループ */
  do{
    if( ((sockMng *)sockmng)->status == THREADSTOP ){
      break;
    }
  }while(TRUE);

#ifdef DEBUG
  printf("SockID:%d thread stop.\n",sockid);
#endif
  ((sockMng *)sockmng)->status = THREADSTOP ;
  
  ExitThread(TRUE);
  // 戻り値を書く必要がある? 
}




/* socket を生成、bindし、listen するまで */
/* 引数はポート番号(文字リテラルでポートを記載 ex. "1100" ) LPSTR とはそういうものらしい */
SOCKET makeListenSocket(LPSTR localportnum){

  struct sockaddr_in addr;
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
  if ( listen( retSocket, SOMAXCONN ) == SOCKET_ERROR ) {
    printf( "Listen failed with error: %ld\n", WSAGetLastError() );
    closesocket(retSocket);
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }

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

  return retSocket ;

}



int senddata(int sockid, SOCKET sockfrom , char *buf , int buflen , SOCKET sockto){ 
  /*
    return:
     >0 : data received/send
     0  : connection close
     -1 : no data
     <0 : receive fail/send failed
   */
  int iResult;
#ifdef DEBUG
  printf("sockid:%d is in senddata()\n");
#endif
  
  iResult = send(sockfrom, buf, strlen(buf), 0);
#ifdef DEBUG
  printf("sockid:%d/senddata reutrn:%d\n",sockid, iResult);
#endif
    
  if (iResult == 0){
    /* 1: sockfrom からのコネクションがクローズしちゃった→セッション終了 */
#ifdef DEBUG
    printf("sockid:%d/senddata Connection closing...\n",sockid);
#endif
    
    /* shutdown を試みる */
    if (SOCKET_ERROR == shutdown(sockfrom, SD_BOTH) ) {

#ifdef DEBUG
      printf("sockid:%d/socket shutdown/senddata failed: %d\n", sockid, WSAGetLastError());
#endif
      
    }
    
#ifdef DEBUG
    printf("sockid:%d/Sockets will close\n",sockid);
#endif
    closesocket(sockfrom);
    closesocket(sockto);
    return iResult ;

    
  }else if (iResult < -1 ){  /* for send error */

    /* from からの送信で失敗 → コネクションが異常とみなしセッション終了 */
#ifdef DEBUG
    printf("sockid:%d/return:%d/senddata failed: %d\n", sockid, iResult, WSAGetLastError());
#endif
    closesocket(sockfrom);
    closesocket(sockto);
    return iResult;

  }else if(iResult == SOCKET_ERROR ){ /* for send's error */

#ifdef DEBUG
    printf("sockid:%d/return:%d/senddata failed: %d\n", sockid, iResult, WSAGetLastError());
#endif
    closesocket(sockfrom);
    closesocket(sockto);
    return -1;
    
  }else{

    /* iRresult >0 OR -1=no data */
#ifdef DEBUG
    printf("sockid:%d/senddata return:%d \'%s\'\n", sockid, iResult, convbuf(buf));
#endif
    return iResult;
  }
}


int recvdata(int sockid, SOCKET sockfrom , char *buf , int buflen , SOCKET sockto){ 
  /*
    return:
     >0 : data received/send
     0  : connection close
     -1 : no data
     <0 : receive fail/send failed
   */
  int iResult;

  iResult = recv(sockfrom, buf, sizeof(buf), 0);
  
  if (iResult == 0){
    /* 1: sockfrom からのコネクションがクローズしちゃった→セッション終了 */
    printf("sockid:%d/recvdata Connection closing...\n",sockid);

    /* shutdown を試みる */
    if (SOCKET_ERROR == shutdown(sockfrom, SD_BOTH) ) {

#ifdef DEBUG
      printf("sockid:%d/socket shutdown/recvdata failed: %d\n", sockid, WSAGetLastError());
#endif
      
    }
    
#ifdef DEBUG
    printf("sockid:%d/Sockets will close\n",sockid);
#endif
    closesocket(sockfrom);
    closesocket(sockto);
    return iResult ;

    
  }else if (iResult < -1 ){  /* for recv's error */

    /* from からの受信で失敗 → コネクションが異常とみなしセッション終了 */
#ifdef DEBUG
    printf("sockid:%d/return:%d/recvdata failed: %d\n", sockid, iResult, WSAGetLastError());
#endif
    closesocket(sockfrom);
    closesocket(sockto);
    return iResult;

  }else if(iResult == SOCKET_ERROR ){ /* for recv's error */

#ifdef DEBUG
    printf("sockid:%d/return:%d/recvdata failed: %d\n", sockid, iResult, WSAGetLastError());
#endif
    closesocket(sockfrom);
    closesocket(sockto);
    return -1;
    
  }else{

    /* iRresult >0 OR -1=no data */
#ifdef DEBUG
    printf("sockid:%d/recvdata return:%d \'%s\'\n", sockid, iResult, convbuf(buf));
#endif
    return iResult;
  }
}


char *convbuf(char *str){
  char *ret;
  int l = strlen(str);
  int p = 0;
  ret = (char*)malloc(sizeof(char)*(l+1));
  
  while(l>=p){
    if( str[p] != '\n' ){
      ret[p] = str[p];
    }else{
      ret[p] = '\\';
      ret[++p] = 'n';
      ret[++p] = '\0'; 
    }

    p++;
  }

  return ret;
}
       
    
