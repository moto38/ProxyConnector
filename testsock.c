#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

HWND g_hwndListBox = NULL;
BOOL g_bExitThread = FALSE;

SOCKET InitializeWinsock(LPSTR lpszPort);
DWORD WINAPI ThreadProc(LPVOID lpParamater);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hinstPrev, LPSTR lpszCmdLine, int nCmdShow)
{
	TCHAR      szAppName[] = TEXT("sample-server");
	HWND       hwnd;
	MSG        msg;
	WNDCLASSEX wc;

	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.style         = 0;
	wc.lpfnWndProc   = WindowProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hinst;
	wc.hIcon         = (HICON)LoadImage(NULL, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED);
	wc.hCursor       = (HCURSOR)LoadImage(NULL, IDC_ARROW, IMAGE_CURSOR, 0, 0, LR_SHARED);
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = szAppName;
	wc.hIconSm       = (HICON)LoadImage(NULL, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED);
	
	if (RegisterClassEx(&wc) == 0)
		return 0;

	hwnd = CreateWindowEx(0, szAppName, szAppName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hinst, NULL);
	if (hwnd == NULL)
		return 0;

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);
	
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HANDLE hThread = NULL;
	static SOCKET socListen = INVALID_SOCKET;
	
	switch (uMsg) {

	case WM_CREATE: {
		DWORD dwThreadId;
		
		g_hwndListBox = CreateWindowEx(0, TEXT("LISTBOX"), NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 0, 0, hwnd, (HMENU)1, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
		
		socListen = InitializeWinsock("4000");
		if (socListen == INVALID_SOCKET)
			return -1;
		
		hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc, &socListen, 0, &dwThreadId);

		return 0;
	}

	case WM_SIZE:
		MoveWindow(g_hwndListBox, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
		return 0;

	case WM_DESTROY:
		if (hThread != NULL) {
			g_bExitThread = TRUE;
			WaitForSingleObject(hThread, 1000);
			CloseHandle(hThread);
		}

		if (socListen != INVALID_SOCKET) {
			closesocket(socListen);
			WSACleanup();
		}

		PostQuitMessage(0);
		return 0;

	default:
		break;

	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

DWORD WINAPI ThreadProc(LPVOID lpParamater)
{
	SOCKET  socListen = *((SOCKET *)lpParamater);
	SOCKET  socServer[10];
	fd_set  fdRead;
	struct timeval timeout;
	int     nMaxSocketCount = 10;
	int     i;
	int     nResult;

	for (i = 0; i < nMaxSocketCount; i++)
		socServer[i] = INVALID_SOCKET;

	timeout.tv_sec  = 0;
	timeout.tv_usec = 500 * 1000;
	
	while (!g_bExitThread) {
		FD_ZERO(&fdRead);
		FD_SET(socListen, &fdRead);

		for (i = 0; i < nMaxSocketCount; i++) {
			if (socServer[i] != INVALID_SOCKET)
				FD_SET(socServer[i], &fdRead);
		}

		nResult = select(0, &fdRead, NULL, NULL, &timeout);
		if (nResult == SOCKET_ERROR) {
			SendMessage(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)TEXT("selectの実行に失敗しました。"));
			break;
		}
		else if (nResult == 0)
			continue;
		else
			;
		
		if (FD_ISSET(socListen, &fdRead)) {
			int              nAddrLen;
			char             szHostName[256];
			char             szBuf[256];
			SOCKADDR_STORAGE sockAddr;

			for (i = 0; i < nMaxSocketCount; i++) {
				if (socServer[i] == INVALID_SOCKET)
					break;
			}

			if (i == nMaxSocketCount) {
				closesocket(accept(socListen, NULL, NULL));
				continue;
			}

			nAddrLen = sizeof(SOCKADDR_STORAGE);
			socServer[i] = accept(socListen, (LPSOCKADDR)&sockAddr, &nAddrLen);
			getnameinfo((LPSOCKADDR)&sockAddr, nAddrLen, szHostName, sizeof(szHostName), NULL, 0, 0);

			wsprintfA(szBuf, "No%d(%s) 接続", i + 1, szHostName);
			SendMessageA(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)szBuf);
		}
		else {
			int   nResult;
			int   nLen;
			TCHAR szData[256];
			TCHAR szBuf[256];
			
			for (i = 0; i < nMaxSocketCount; i++) {
				if (socServer[i] != INVALID_SOCKET && FD_ISSET(socServer[i], &fdRead))
					break;
			}

			nLen = sizeof(szData);
			nResult = recv(socServer[i], (char *)szData, nLen, 0);
			if (nResult == 0) {
				wsprintf(szBuf, TEXT("No%d 切断"), i + 1);
				SendMessage(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)szBuf);

				shutdown(socServer[i], SD_BOTH);
				closesocket(socServer[i]);
				socServer[i] = INVALID_SOCKET;
			}
			else if (nResult > 0) {
				wsprintf(szBuf, TEXT("No%d %s"), i + 1, szData);
				SendMessage(g_hwndListBox, LB_ADDSTRING, 0, (LPARAM)szBuf);
			
				nLen = nResult;
				nResult = send(socServer[i], (char *)szData, nLen, 0);
			}
			else
				;
		}
	}

	return 0;
}

SOCKET InitializeWinsock(LPSTR lpszPort)
{
	WSADATA    wsaData;
	ADDRINFO   addrHints;
	LPADDRINFO lpAddrList;
	SOCKET     socListen;
	
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	//ZeroMemory(&addrHints, sizeof(addrinfo));
	ZeroMemory(&addrHints, sizeof(ADDRINFO));
	addrHints.ai_family   = AF_INET;
	addrHints.ai_socktype = SOCK_STREAM;
	addrHints.ai_protocol = IPPROTO_TCP;
	addrHints.ai_flags    = AI_PASSIVE;

	if (getaddrinfo(NULL, lpszPort, &addrHints, &lpAddrList) != 0) {
		MessageBox(NULL, TEXT("ホスト情報からアドレスの取得に失敗しました。"), NULL, MB_ICONWARNING);
		WSACleanup();
		return INVALID_SOCKET;
	}

	socListen = socket(lpAddrList->ai_family, lpAddrList->ai_socktype, lpAddrList->ai_protocol);
	
	if (bind(socListen, lpAddrList->ai_addr, (int)lpAddrList->ai_addrlen) == SOCKET_ERROR) {
		MessageBox(NULL, TEXT("ローカルアドレスとソケット関連付けに失敗しました。"), NULL, MB_ICONWARNING);
		closesocket(socListen);
		freeaddrinfo(lpAddrList);
		WSACleanup();
		return INVALID_SOCKET;
	}
	
	if (listen(socListen, 1) == SOCKET_ERROR) {
		closesocket(socListen);
		freeaddrinfo(lpAddrList);
		WSACleanup();
		return INVALID_SOCKET;
	}
	
	freeaddrinfo(lpAddrList);

	return socListen;
}
