#undef UNICODE

#define WIN32_LEAN_AND_MEAN
#define TEMP_NOUSE

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
//#define DEFAULT_PORT "18080"
#define DEFAULT_BUFLEN 4096

#define THREADREADY 0
#define THREADRUNNING 1
#define THREADSTOP 2

//#define MAXCONNECTION 256
#define MAXCONNECTION 2

#define USE_SELECT

#define ParentProxyHost "localhost"
#define ParentProxyPort "8080"  //"4096" //


SOCKET makeListenSocket(LPSTR);
SOCKET makeConnectSocket(char* HOSTNAME , LPSTR);
DWORD WINAPI waitRecieveThread(LPVOID);
int sendrecvdata(int , char , SOCKET , char* , int , SOCKET);
char *convbuf(char*);

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
  
  fd_set fds, readfds;
  struct timeval timeout;

  DWORD dwNonBlocking = 1;

  timeout.tv_sec = 5;
  timeout.tv_usec = 1000*500;

  

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
  if( ListenSocket == INVALID_SOCKET || ListenSocket == SOCKET_ERROR ){
    printf("makeListenSocket fail: %d\n", WSAGetLastError());
    return 1;
  }else{
#ifdef DEBUG
    printf("makeListenSocket succeed ListenSocket: %d\n", ListenSocket);
#endif
    ;
  }

  ioctlsocket(ListenSocket, FIONBIO, &dwNonBlocking);

  FD_ZERO(&readfds);
  FD_SET(ListenSocket ,&readfds);

#ifdef DEBUG
  printf("FD_SET(ListenSocket, &readfds)  readfds:%d\n", readfds);
#endif

  while(1){
    memcpy(&fds, &readfds, sizeof(fd_set));    
    iResult = select(256, &fds, NULL, NULL, &timeout);
    //iResult = select(0, &fds, NULL, NULL, NULL);
  
    if( iResult == 0 ){
      /* timeout */
#ifdef DEBUG
      printf("ListenSocket select timeout\n");
#endif
      continue;
    }else if( iResult < 0 ){
      /* Error */
#ifdef DEBUG
      printf("ListenSocket select Error select:%d\n", iResult);
      printf("Error at select(): %ld\n", WSAGetLastError());
#endif
      break ;
      
    }else{
      //if( iResult != 0){
#ifdef DEBUG
      printf("ListenSocket:%d fds:%d\n",ListenSocket, fds);
#endif
      if(FD_ISSET(ListenSocket , &fds)){
	/* socket connection come */
	AddrLen = sizeof(SOCKADDR_STORAGE);
	//AddrLen = sizeof(sockaddr_in);
	ClientSocket = accept(ListenSocket, (LPSOCKADDR)&sockAddr, &AddrLen);
	// getnameinfo( (LPSOCKADDR)&sockAddr, AddrLen, (char*)HostName, sizeof(HostName), NULL, 0, 0);

	if (ClientSocket == INVALID_SOCKET || ClientSocket == SOCKET_ERROR ) {
	  printf("accept failed: %d\n", WSAGetLastError());
	  closesocket(ListenSocket);
	  WSACleanup();
	  return 1;
	}
      
	iResult = -1;
	for(int i=0; i<MAXCONNECTION; i++){
	  if( sockmng[i].status == THREADREADY ){ /* THREADREADYなら CreateThread し、iResult=i。ループが最後までいった場合は「thread生成に失敗した」ということでiResult=-1のまま */
	    
#ifdef DEBUG	
	    printf("sockid:%d\n",i);
#endif //DEBUG       
	    sockmng[i].status = THREADRUNNING;
	    sockmng[i].csocket = ClientSocket ;
	    sockmng[i].hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)waitRecieveThread, &(sockmng[i]), 0, &dwThreadID); /* ひきすうは自IDなど含んだ構造体にする事。終了ステータスを返すため */
	    iResult = i;
	    break;
	  }
	}
    
	if( iResult == -1 ){
	  printf("can not make Thread: %d\n",WSAGetLastError());
	}
      }
    }
  }

  for(int i=0; i<MAXCONNECTION; i++){   
    CloseHandle(sockmng[i].hThread);
  }
  
  WSACleanup();
  return 0;
}



/* 受信スレッドルーティン */
DWORD WINAPI waitRecieveThread(LPVOID sockmng){ //lpClientSocket -> sockmng 修正
  /* LPVOID型 引数のarglistとして渡される。
  よびだし側は (HANDLE)hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)waitReceiveThread, &(sockmng[i]), 0, &dwThreadID);
  */

  char clrecvbuf[DEFAULT_BUFLEN], cnrecvbuf[DEFAULT_BUFLEN];
  
  int fdissetResult , iResult ,nfds=0 ;
  int recvbuflen = DEFAULT_BUFLEN , cnrecvbuflen, clrecvbuflen;
  
  int sockid = ((sockMng *)sockmng)->sockid; 
  SOCKET ClientSocket ;
  SOCKET ConnectSocket ;
  

  fd_set readfds, fds;
  struct timeval timeout;
  int nResult;

  DWORD dwNonBlocking = 1;

  timeout.tv_sec = 0 ; //3 ;
  timeout.tv_usec = 1000 ; //500*1000 ;

  memcpy(clrecvbuf , "\0" , 1);
  memcpy(cnrecvbuf , "\0" , 1);
  

  
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
  /*
  ioctlsocket(ClientSocket, FIONBIO, &dwNonBlocking);
  ioctlsocket(ConnectSocket, FIONBIO, &dwNonBlocking);
  */

  FD_ZERO(&readfds);
  FD_SET(ClientSocket,  &readfds);
  FD_SET(ConnectSocket, &readfds);


  /* ループ */
  do{
    memcpy(&fds, &readfds, sizeof(fd_set));
    //nResult = select(0, &fds, NULL, NULL, &timeout);
    nResult = select(0, &fds, NULL, NULL, NULL);
    
    //#ifdef DEBUG
    //    printf("select return: nResul=%d\n",nResult);
    //    printf("fds:%d/Address:%d\n",fds, &fds);
    //    printf("select return: WSAGetLastError:%d\n", WSAGetLastError());
    //#endif
	
    /* recvbuf が「データあり」ならば対向側に送信 */
    /* char* のデータにモノが入っているかどうかをどう調べる? */
    /* recvbuf は 一文字目がNULLならば空と見做すで大丈夫かどうか? */

    if (nResult != 0){
      /* Socketに受信あり */
      //#ifdef DEBUG
      //      printf("SockID:%d select return=%d",sockid , nResult);
      //#endif
      
      fdissetResult =  FD_ISSET(ClientSocket, &fds);
      if (fdissetResult){
	/* Client から受信 データは clrecvbuf にきろくされる
	   >0 : data received
	   0  : connection close (closed 済みで returnしてくる)
	   -1 : no data (closed 済みで returnしてくる)
	   <0 : receive fail (closed 済みで returnしてくる)
	*/
#ifdef DEBUG
	printf("SockID:%d/ClientSocket's FD_ISSET=%d\n",sockid, fdissetResult);
#endif //DEBUG
	
	iResult = recvdata(sockid, ClientSocket, clrecvbuf, recvbuflen, ConnectSocket);
	
	/* iResult がエラーやセッションクローズだったら do ループを break してスレッドを終了してmainに戻る */
	/* >0 データがあるのなら読み取り send する */
	/* -1 データがないのなら次のチェックへ(ConnectSocketのチェックへ) */
	if( iResult < 0 ){
	  FD_CLR(ClientSocket, &fds);
	  FD_CLR(ConnectSocket, &fds);
	  break;
	}else{
	  clrecvbuflen = iResult ;
#ifdef DEBUG
	  printf("SockID:%d has data from client, invoke senddata().\n",sockid);
#endif
      
	  iResult = senddata(sockid, ConnectSocket, clrecvbuf, clrecvbuflen, ClientSocket);
#ifdef DEBUG
	  printf("SockID:%d/senddata to ConnectSocket return:%d\n",sockid, iResult);
#endif //DEBUG
	  if( iResult < 0 ){
	    FD_CLR(ClientSocket, &fds);
	    FD_CLR(ConnectSocket, &fds);
	    break;
	  }else{
	    clrecvbuf[0] = '\0';
	    clrecvbuflen = 0;
	    // continue;
	  }
	}
	
      }
      
      fdissetResult = FD_ISSET(ConnectSocket, &fds);
      if (fdissetResult){
	/* ConnectServer から受信 データは cnrecvbuf にきろくされる
	   >0 : data received
	   0  : connection close (closed 済みで returnしてくる)
	   -1 : no data (closed 済みで returnしてくる)
	   <0 : receive fail (closed 済みで returnしてくる)
	*/
#ifdef DEBUG
	printf("SockID:%d/ConnectSocket's FD_ISSET=%d\n",sockid, fdissetResult);
#endif //DEBUG
	
	iResult = recvdata(sockid, ConnectSocket, cnrecvbuf, recvbuflen, ClientSocket);
	/* iResult がエラーやセッションクローズだったら do ループを break してスレッドを終了してmainに戻る */
	/* >0 データがあるのなら読み取り send する */
	/* -1 データがないのなら次のチェックへ(ConnectSocketのチェックへ) */
	if( iResult < 0 ){
	  FD_CLR(ConnectSocket, &fds);
	  FD_CLR(ClientSocket, &fds);
	  break;
	}else{
	  cnrecvbuflen = iResult ;
#ifdef DEBUG
	  printf("SockID:%d has data from connect server, invoke senddata().\n",sockid);
#endif
	  
	  iResult = senddata(sockid, ClientSocket, cnrecvbuf, cnrecvbuflen, ConnectSocket);
#ifdef DEBUG
	  printf("SockID:%d/senddata to ClientSocket return:%d\n",sockid, iResult);
#endif //DEBUG
	  if( iResult < 0 ){
	    FD_CLR(ClientSocket, &fds);
	    FD_CLR(ConnectSocket, &fds);
	    break;
	  }else{
	    cnrecvbuf[0] = '\0';
	    cnrecvbuflen = 0;
	    //continue;
	  }
	}
      }
    }
    //memcpy(&fds, &readfds, sizeof(fd_set));

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

#ifdef TEMP_NOUSE
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
#else
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(localportnum));
  addr.sin_addr.S_un.S_addr = INADDR_ANY;
#endif


  // Create a SOCKET for the server to listen for client connections
#ifdef TEMP_NOUSE
  retSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
#else
  retSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
  if (retSocket == INVALID_SOCKET) {
    printf("Error at socket(): %ld\n", WSAGetLastError());
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }
  
  // Setup the TCP listening socket(BIND)
#ifdef TEMP_NOUSE
  iResult = bind( retSocket, result->ai_addr, (int)result->ai_addrlen);
#else
  iResult = bind( retSocket, (struct sockaddr *)&addr, sizeof(addr));
#endif
  if (iResult == SOCKET_ERROR) {
    printf("bind failed with error: %d\n", WSAGetLastError());
    closesocket(retSocket);
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }
  
  
  // Setup the TCP listening socket(listen)
  if ( listen( retSocket, SOMAXCONN ) == SOCKET_ERROR ) {
  //if ( listen( retSocket, 15 ) == SOCKET_ERROR ) {
    printf( "Listen failed with error: %ld\n", WSAGetLastError() );
    closesocket(retSocket);
    freeaddrinfo(result);
    WSACleanup();
    return INVALID_SOCKET ;
  }

  // free 'reslut'(getaddrinfo's result)
#ifdef TEMP_NOUSE
  freeaddrinfo(result);
#endif
  
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
  //freeaddrinfo(result);

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
  
  iResult = send(sockfrom, buf, buflen, 0);
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

  iResult = recv(sockfrom, buf, buflen, 0);
  
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
    printf("sockid:%d/recvdataa return:%d \'%s\'\n", sockid, iResult, convbuf(buf));
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
       
    
