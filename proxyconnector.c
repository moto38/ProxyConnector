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
//#define DEFAULT_PORT "18080"
#define DEFAULT_BUFLEN 4096

#define THREADREADY 0
#define THREADRUNNING 1
#define THREADSTOP 2

#define MAXCONNECTION 256

#define USE_SELECT

#define ParentProxyHost "localhost"
#define ParentProxyPort "4096" //"8080"  //"4096" //

#define SEND 0
#define RECV 1


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
      /*
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

  WSACleanup();
  return 0;
  
}



/* 受信スレッドルーティン */
DWORD WINAPI waitRecieveThread(LPVOID sockmng){ //lpClientSocket -> sockmng 修正
  /* LPVOID型 引数のarglistとして渡される。
  よびだし側は (HANDLE)hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)waitReceiveThread, &(sockmng[i]), 0, &dwThreadID);
  */

  char clrecvbuf[DEFAULT_BUFLEN], cnrecvbuf[DEFAULT_BUFLEN];
  clrecvbuf[0] = '\0' ;
  cnrecvbuf[0] = '\0' ;
  
  int iSendResult , iResult ;
  int recvbuflen = DEFAULT_BUFLEN , cnrecvbuflen, clrecvbuflen;
  
  int sockid = ((sockMng *)sockmng)->sockid; 
  SOCKET ClientSocket = ((sockMng *)sockmng)->csocket; /* sockmng を sockMng * にCASTして、そのcsocketを取り出し ClientSocket へ入れる */
  SOCKET ConnectSocket ;
  

  fd_set fdReadAtRcvThread , fds;
  struct timeval timeoutAtRcvThread;
  int nResultAtRcvThread;

  timeoutAtRcvThread.tv_sec = 3 ;
  timeoutAtRcvThread.tv_usec = 0 ; //500*1000 ;


  DWORD dwNonBlocking = 1;

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

  ioctlsocket(ClientSocket, FIONBIO, &dwNonBlocking);
  ioctlsocket(ConnectSocket, FIONBIO, &dwNonBlocking);
  
  /* ループ */
  //do {
    /* 
       ClientRecv
         1:reslut==0 -> connection close
         2:result< 0 -> recv failed / connection close
         3:result> 0 -> success
           ConnectSend
             4:result==SCOKET_ERROR -> connection close
    */


  FD_ZERO(&fds);
  FD_SET(ClientSocket,  &fds);
  FD_SET(ConnectSocket, &fds);
  
  do{
    
    memcpy(&fdReadAtRcvThread, &fds, sizeof(fd_set));
    
    nResultAtRcvThread = select(0, &fdReadAtRcvThread, NULL, NULL, &timeoutAtRcvThread);

#ifdef DEBUG    
    printf("nResultAtrcvthread=%d\n",nResultAtRcvThread);
#endif
    
    /*if (nResultAtRcvThread == SOCKET_ERROR) {
      printf("selectの実行に失敗しました。\n");
      closesocket(ClientSocket);
      closesocket(ConnectSocket);
      break;
      } else*/

    /* recvbuf が「データあり」ならば対向側に送信 */
    /* char* のデータにモノが入っているかどうかをどう調べる? */
    /* recvbuf は 一文字目がNULLならば空と見做すで大丈夫かどうか? */

    if ( clrecvbuf[0] != '\0' ){

      iResult = sendrecvdata(sockid, SEND, ConnectSocket, clrecvbuf, clrecvbuflen, ClientSocket);

      clrecvbuf[0] = '\0';
      clrecvbuflen = 0;
      continue;

    }else if ( cnrecvbuf[0] != '\0' ){
      
      iResult = sendrecvdata(sockid, SEND, ClientSocket, cnrecvbuf, cnrecvbuflen, ConnectSocket);

      cnrecvbuf[0] = '\0';
      cnrecvbuflen = 0;
      continue;
      
    }
      
    
    if (nResultAtRcvThread == 0){ //何もソケットに来ていない
      continue;
    }else{ 
      /* Socketに受信あり */
      
      if (FD_ISSET(ClientSocket, &fdReadAtRcvThread)){
	/* Client から受信 データは clrecvbuf にきろくされる
	   >0 : data received
	   0  : connection close (closed 済みで returnしてくる)
	   -1 : no data (closed 済みで returnしてくる)
	   <0 : receive fail (closed 済みで returnしてくる)
	*/
	iResult = sendrecvdata(sockid, RECV, ClientSocket, clrecvbuf, recvbuflen, ConnectSocket);

	/* iResult がエラーやセッションクローズだったら do ループを break してスレッドを終了してmainに戻る */
	/* >0 データがあるのなら読み取り send する */
	/* -1 データがないのなら次のチェックへ(ConnectSocketのチェックへ) */
	if( iResult < 0 ){
	  break;
	}else{
	  clrecvbuflen = iResult ;
	  continue;
	}
	
      }// if FD_ISSET(ClientSocket, ,,,)
      //else 
      
      if (FD_ISSET(ConnectSocket, &fdReadAtRcvThread)){
	/* ConnectServer から受信 データは cnrecvbuf にきろくされる
	   >0 : data received
	   0  : connection close (closed 済みで returnしてくる)
	   -1 : no data (closed 済みで returnしてくる)
	   <0 : receive fail (closed 済みで returnしてくる)
	*/
	iResult = sendrecvdata(sockid, RECV, ConnectSocket, cnrecvbuf, recvbuflen, ClientSocket);

	/* iResult がエラーやセッションクローズだったら do ループを break してスレッドを終了してmainに戻る */
	/* >0 データがあるのなら読み取り send する */
	/* -1 データがないのなら次のチェックへ(ConnectSocketのチェックへ) */
	if( iResult < 0 ){
	  break;
	}else{
	  cnrecvbuflen = iResult ;
	  continue;
	}
	
      }// if FD_ISSET(ConnectSocket, ,,,)
	
      
    }
  }while (iResult > 0);
    
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
  //freeaddrinfo(result);

  return retSocket ;

}


int sendrecvdata(int sockid, char mode , SOCKET sockfrom , char *recvbuf , int recvbuflen , SOCKET sockto){ 
  /*
    return:
     >0 : data received/send
     0  : connection close
     -1 : no data
     <0 : receive fail/send failed
   */
  int iResult;
  char *mode_str ; //[2][5] = { "RECV" , "SEND" };

  if( mode == RECV ){
    iResult = recv(sockfrom, recvbuf, recvbuflen, 0);
#ifdef DEBUG
    mode_str = "RECV" ;
#endif //DEBUG
  }else{
    iResult = send(sockfrom, recvbuf, recvbuflen, 0);
#ifdef DEBUG
    mode_str = "SEND" ; 
#endif //DEBUG
  }
  
  if (iResult == 0){
    /* 1: sockfrom からのコネクションがクローズしちゃった→セッション終了 */
    printf("sockid:%d/Connection closing...\n",sockid);

    /* shutdown を試みる */
    if (SOCKET_ERROR == shutdown(sockfrom, SD_BOTH) ) {

#ifdef DEBUG
      printf("sockid:%d/socket shutdown/%s failed: %d\n", sockid, mode_str, WSAGetLastError());
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
    printf("sockid:%d/return:%d/%s failed: %d\n", sockid, iResult, mode_str, WSAGetLastError());
#endif
    closesocket(sockfrom);
    closesocket(sockto);
    return iResult;

  }else if(iResult == SOCKET_ERROR ){ /* for send's error */

#ifdef DEBUG
    printf("sockid:%d/return:%d/%s failed: %d\n", sockid, iResult, mode_str, WSAGetLastError());
#endif
    closesocket(sockfrom);
    closesocket(sockto);
    return -1;
    
  }else{

    /* iRresult >0 OR -1=no data */
#ifdef DEBUG
    printf("sockid:%d/%s return:%d \'%s\'\n", sockid, mode_str, iResult, convbuf(recvbuf));
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
