/*
	Android App Server - Written by FrOzeN89 - 22/3/2014

	http://www.reddit.com/u/frozen89

	This code is just a demo for usage with Android App Client.
*/

#include <windows.h>
#include <string>
#include <sstream>
#include <ctime>
#include <vector>
#include <map>
#include <algorithm>
#include <list>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")

#define WM_SOCKET		(WM_USER + 1)
#define BUFFER_LENGTH	8192

#define PKT_USERINFO	1
#define PKT_FILE		2

const wchar_t *ClassName = L"AndroidAppServer";

// Hardcoded database to save time making this server
#define USERS_COUNT 2

// User structure
struct User
{
	std::wstring	Username;
	std::wstring	Password;
	bool			LoggedIn;
};

User Users[USERS_COUNT] = { L"FrOzeN", L"test123", false,
							L"prlmike", L"pAss2014", false };

// Function declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void SetFont(HWND hWnd);
void AddLog(std::wstring Message);
std::wstring GetTime();
int FindIndex(SOCKET Socket);
bool MatchString(std::wstring First, std::wstring Second, bool CaseSensitive = false);
void DisplayFile(std::string Filename, const int Position, const int Size);

// Winsock functions & variables
bool StartWinsock();
int StartListen(int Port);
std::wstring GetLocalIP();
void CloseWinsock();

SOCKET SocketListen;

HINSTANCE WinInstance;
const DWORD DefaultStyles = WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE;

// Window handles for controls and main window
HWND fMain, lPort, tPort, bStart, bClose, tLog, lsClients, lsFiles;
const HMENU IDM_START = (HMENU)1;
const HMENU IDM_CLOSE = (HMENU)2;

// File class
class File
{
public:
	File(std::string Filename, int Size) :
										   m_Filename(Filename),
										   m_Size(Size)
	{
		// Open file for binary writing
		m_Stream = std::ofstream("imgs\\" + m_Filename, std::ios::binary);

		// Display file in listbox lsFiles
		std::wstringstream Text;
		Text << std::wstring(m_Filename.begin(), m_Filename.end()) << L" - (0 / " << m_Size << L") [0% Complete]";

		SendMessage(lsFiles, LB_ADDSTRING, 0, (LPARAM)Text.str().c_str());
	};

	~File()
	{
		m_Stream.close();
	};
	
	std::string Filename() const {return m_Filename;};

	void Write(std::vector<char> *Data)
	{
		// Write data to end of file
		m_Stream.write(&(*Data)[0], Data->size());

		// Increase position to track how much data has been written
		m_Position += Data->size();

		// Update file progress
		DisplayFile(m_Filename, m_Position, m_Size);
	};

	bool Complete()
	{
		if (m_Position == m_Size)
			return true;
		
		return false;
	};

private:
	std::ofstream	m_Stream;
	const int		m_Size;
	int				m_Position;
	std::string		m_Filename;
};

std::list<File *>	Files;

// Packet class
class Packet
{
public:
	Packet(char PacketHeader) :
								m_Header(PacketHeader)
	{};

	Packet(std::vector<char> Data) :
									 m_Data(Data)
	{
		// Strip packet header information
		m_Header = m_Data[0];
		memcpy(&m_Length, &m_Data[1], 4);
		m_Data.erase(m_Data.begin(), m_Data.begin() + 5);
	};

	char Header() const {return m_Header;};
	DWORD Length() const {return m_Length;};

	std::vector<char> *RawData() {return &m_Data;};

	void InsertByte(char Data)
	{
		m_Data.push_back(Data);
	};

	// Remove DWORD
	DWORD RemoveDWORD()
	{
		DWORD Data;
		memcpy(&Data, &m_Data[0], 4);

		m_Data.erase(m_Data.begin(), m_Data.begin() + 4);

		return Data;
	};

	// Remove Null-Terminated string from packet
	std::wstring RemoveNTString()
	{
		std::wstring NTString;
		int i = 0;

		while (m_Data[i])
			NTString.push_back(m_Data[i++]);

		m_Data.erase(m_Data.begin(), m_Data.begin() + i + 1);
		
		return NTString;
	};

	void SendPacket(SOCKET Socket)
	{
		char HeaderInfo[5];

		HeaderInfo[0] = m_Header;
		m_Length = m_Data.size() + 5;
		memcpy(&HeaderInfo[1], &m_Length, 4);

		m_Data.insert(m_Data.begin(), HeaderInfo, HeaderInfo + 5);

		send(Socket, &m_Data[0], m_Data.size(), 0);
	};

private:
	char				m_Header;
	DWORD				m_Length;
	std::vector<char>	m_Data;
};

// Client class
class Client
{
public:
	Client(SOCKET ListeningSocket) :
									 m_ConnectionStart(GetTickCount())
	{
		m_Socket = accept(ListeningSocket, NULL, NULL);
		int Result = WSAAsyncSelect(m_Socket, fMain, WM_SOCKET, FD_WRITE | FD_READ | FD_CLOSE);

		std::wstringstream ws;
		ws << (int)m_Socket;
		m_wsSocket = ws.str();
		SendMessage(lsClients, LB_ADDSTRING, 0, (LPARAM)m_wsSocket.c_str());

		AddLog(L"Socket #" + ws.str() + L" connected.");
	};

	~Client()
	{
		AddLog(L"Socket #" + m_wsSocket + L" disconnected.");

		int Index = FindIndex(m_Socket);

		if (Index >= 0)
			SendMessage(lsClients, LB_DELETESTRING, (WPARAM)Index, 0);

		for (int i = 0; i < USERS_COUNT; ++i)
			if (MatchString(m_Username, Users[i].Username))
			{
				Users[i].LoggedIn = false;
				break;
			}
	};

	DWORD Uptime()
	{
		return GetTickCount() - m_ConnectionStart;
	};

	SOCKET Socket() {return m_Socket;};

	void ParsePacket(Packet &Data)
	{
		// Only process if receiving PKT_USERINFO or the user is already logged in
		if ((Data.Header() == PKT_USERINFO) || (m_Username.length()))
			switch (Data.Header())
			{
				case PKT_USERINFO:
				{
					AddLog(L"Received PKT_USERINFO from socket " + m_wsSocket + L".");

					char Result = 0;
					std::wstring Username = Data.RemoveNTString();
					std::wstring Password = Data.RemoveNTString();

					for (int i = 0; i < USERS_COUNT; ++i)
						// Check if Username and Password matches
						if (MatchString(Username, Users[i].Username) && MatchString(Password, Users[i].Password, true))
						{
							// If user already logged in then fail new login attempt
							if (Users[i].LoggedIn)
								break;

							Users[i].LoggedIn = true;
							m_Username = Users[i].Username;
							Result = 1;
							break;
						}

					Packet UserInfo(PKT_USERINFO);
					UserInfo.InsertByte(Result);
					UserInfo.SendPacket(m_Socket);

					AddLog(L"Sent PKT_USERINFO to " + m_wsSocket + L".");

					int Index = FindIndex(m_Socket);

					if (Index >= 0)
					{
						SendMessage(lsClients, LB_DELETESTRING, (WPARAM)Index, 0);
						SendMessage(lsClients, LB_INSERTSTRING, (WPARAM)Index, (LPARAM)std::wstring(m_wsSocket + L" - " + m_Username).c_str());
					}
				}
				break;

				case PKT_FILE:
				{
					// Read filename
					std::wstring wsFilename(Data.RemoveNTString());
					std::string sFilename(wsFilename.begin(), wsFilename.end());

					// Check if Filename is in Files list already
					File *CurrentFile = NULL;
					std::list<File *>::const_iterator FindFile = Files.begin();

					while (FindFile != Files.end())
					{
						std::string CheckFile = ((File *)*FindFile)->Filename();

						if (CheckFile == sFilename)
						{
							CurrentFile = *FindFile;
							break;
						}

						++FindFile;
					}

					// If file not found then create new file and add it to Files list
					if (!CurrentFile)
					{
						CurrentFile = new File(sFilename, (int)Data.RemoveDWORD());
						Files.push_back(CurrentFile);
					}

					// Write to file
					CurrentFile->Write(Data.RawData());

					// if completed writing then close file and delete file object
					if (CurrentFile->Complete())
					{
						// Remove file from Files list and delete File object
						Files.erase(FindFile);
						delete CurrentFile;
					}
				}
				break;
			}
	};
	
	// Should be private, but not worth the time encapsulating this
	std::vector<char>	Buffer;

private:
	const DWORD			m_ConnectionStart;
	SOCKET				m_Socket;
	std::wstring		m_wsSocket;
	std::wstring		m_Username;
};

std::map<SOCKET, Client *>	Clients;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	WNDCLASSEX wc;
	MSG Message;

	WinInstance = hInstance;

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = ClassName;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wc))
	{
		MessageBox(NULL, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}

	fMain = CreateWindowEx(
					 	   0,
						   ClassName,
						   L"App Image Server",
						   WS_OVERLAPPED | WS_MINIMIZEBOX | WS_SYSMENU,
						   CW_USEDEFAULT,
						   CW_USEDEFAULT,
						   875,
						   400,
						   NULL,
						   NULL,
						   hInstance,
						   NULL);

	if (fMain == NULL)
	{
		MessageBox(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}

	ShowWindow(fMain, nCmdShow);
	UpdateWindow(fMain);

	while (GetMessage(&Message, NULL, 0, 0) > 0)
	{
		TranslateMessage(&Message);
		DispatchMessage(&Message);
	}

	return Message.wParam;
}

void SetFont(HWND hWnd)
{
	HFONT Font = CreateFont(-MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
		0,
		0,
		0,
		FW_NORMAL,
		(DWORD)false,
		(DWORD)false,
		(DWORD)false,
		ANSI_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE,
		L"Tahoma");

	SendMessage(hWnd, WM_SETFONT, (WPARAM)Font, 0);
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_SOCKET:
		{
			switch (WSAGETSELECTEVENT(lParam))
			{
				case FD_READ:
				{
					// Look pointer to Client using wParam (Socket)
					Client *pClient = NULL;
					if (Clients.find((SOCKET)wParam) != Clients.end())
						pClient = Clients[(SOCKET)wParam];

					// Retrieve data available
					char Buffer[BUFFER_LENGTH];
					int BufferLength = recv((SOCKET)wParam, Buffer, BUFFER_LENGTH, 0);

					if (BufferLength)
					{
						// Append Buffer to Client's Buffer
						pClient->Buffer.insert(pClient->Buffer.end(), Buffer, Buffer + BufferLength);
						
						// Create Data variable and fill with unprocess received data
						std::vector<char> Data(pClient->Buffer);

						do
						{
							// Get length of current packet
							DWORD PacketLength;
							memcpy(&PacketLength, &Data[1], 4);

							/* If length of buffer now exceeds or is equal to size of packet, then
							/		process the packet. Otherwise keep appending newly received
							/		data until a full packet has been accumlated.					*/
							if (Data.size() >= (int)PacketLength)
							{
								// Parse packet data to Client class
								Packet CurrentPacket(std::vector<char>(Data.begin(), Data.begin() + (int)PacketLength));
								pClient->ParsePacket(CurrentPacket);

								// Erase Packet from remaining data
								Data.erase(Data.begin(), Data.begin() + (int)PacketLength);

								// Clear Client's buffered data
								pClient->Buffer.clear();
							}
							else
							{
								// If not enough data for a complete packet, add remaining data to Client's buffer
								pClient->Buffer.insert(pClient->Buffer.end(), Data.begin(), Data.end());
								Data.clear();
							}
						}
						while (!Data.empty());
					}
				}
				break;

				case FD_CLOSE:
				{
					// Look up pointer to Client using wParam (Socket)
					Client *pClient = NULL;
					if (Clients.find((SOCKET)wParam) != Clients.end())
						pClient = Clients[(SOCKET)wParam];

					// Delete Client
					delete pClient;
				}
				break;

				case FD_ACCEPT:
				{
					if (wParam == SocketListen)
					{
						// Create new Client
						Client *NewClient = new Client(SocketListen);
						Clients[NewClient->Socket()] = NewClient;
					}
				}
				break;

				case FD_WRITE:
					//
				break;

				case FD_CONNECT:
					//
				break;

				default:
					;
			}
		}
		break;

		case WM_COMMAND:
		{
			if (LOWORD(wParam) == (WORD)IDM_START)
			{
				// Get port from textbox
				int Length = SendMessage(tPort, WM_GETTEXTLENGTH, NULL, NULL);
				std::wstring Text;

				if (Length > 0)
				{
					std::vector<WCHAR> Buffer(Length + 1);
					SendMessage(tPort, WM_GETTEXT, Buffer.size(), (LPARAM)&Buffer[0]);
					Text = &Buffer[0];
				}

				// Convert variable types wstring -> string -> int
				std::string Port(Text.begin(), Text.end());
				int iStartListen = StartListen(atoi(Port.c_str()));

				if (iStartListen)
				{
					EnableWindow(lPort, (int)false);
					EnableWindow(tPort, (int)false);
					EnableWindow(bStart, (int)false);
					EnableWindow(bClose, (int)true);

					std::wstringstream wsPort;
					wsPort << iStartListen;
					SendMessage(tPort, WM_SETTEXT, NULL, (LPARAM)wsPort.str().c_str());

					AddLog(L"Server listening on " + GetLocalIP() + L" using port " + wsPort.str().c_str() + L".");
				}
				else
					AddLog(L"Failed to start server.");
			}
			else if (LOWORD(wParam) == (WORD)IDM_CLOSE)
			{
				CloseWinsock();

				EnableWindow(bClose, (int)false);
				EnableWindow(lPort, (int)true);
				EnableWindow(tPort, (int)true);
				EnableWindow(bStart, (int)true);

				SendMessage(lsClients, LB_RESETCONTENT, 0, 0);
				SendMessage(lsFiles, LB_RESETCONTENT, 0, 0);

				for (int i = 0; i < USERS_COUNT;)
					Users[i++].LoggedIn = false;

				std::list<File *>::iterator itFile = Files.begin();
				
				// Clear file list and clear remaining objects
				while (itFile != Files.end())
					delete *itFile++;

				Files.clear();

				AddLog(L"Server closed.");
			}			
		}
		break;

		case WM_CREATE:
		{
			// Set hWnd here as WM_CREATE executes before CreateWindowEx() returns
			fMain = hWnd;

			// Start window center screen
			RECT Rect;
			if (GetWindowRect(hWnd, &Rect))
			{
				int Width, Height, Left, Top;
				Width = Rect.right - Rect.left;
				Height = Rect.bottom - Rect.top;

				Left = (GetSystemMetrics(SM_CXSCREEN) - Width) / 2;
				Top = (GetSystemMetrics(SM_CYSCREEN) - Height) / 2;

				SetWindowPos(hWnd, NULL, Left, Top, NULL, NULL, SWP_NOZORDER | SWP_NOSIZE);
			}

			// Create controls
			lPort = CreateWindow(L"EDIT", L"Port:", DefaultStyles | ES_RIGHT, 16, 16, 33, 25, hWnd, 0, 0, 0);
			if (lPort)
				SetFont(lPort);
			tPort = CreateWindowEx(WS_EX_CLIENTEDGE, L"Edit", L"80", DefaultStyles | ES_NUMBER, 56, 16, 81, 24, hWnd, 0, WinInstance, 0);
			if (tPort)
			{
				SetFont(tPort);
				SendMessage(tPort, EM_LIMITTEXT, (WPARAM)5, NULL);	// Set max length to 5 characters
			}
			bStart = CreateWindowEx(0, L"Button", L"&Start Server", DefaultStyles, 144, 8, 113, 33, hWnd, IDM_START, WinInstance, NULL);
			if (bStart)
				SetFont(bStart);
			bClose = CreateWindowEx(0, L"Button", L"&Close Server", DefaultStyles | WS_DISABLED, 264, 8, 113, 33, hWnd, IDM_CLOSE, WinInstance, NULL);
			if (bClose)
				SetFont(bClose);
			tLog = CreateWindowEx(WS_EX_CLIENTEDGE, L"Edit", L"", DefaultStyles | ES_READONLY | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL, 16, 48, 400, 305, hWnd, 0, WinInstance, 0);
			if (tLog)
				SetFont(tLog);
			lsClients = CreateWindowEx(WS_EX_CLIENTEDGE, L"Listbox", L"", DefaultStyles | WS_VSCROLL, 430, 48, 420, 120, hWnd, 0, WinInstance, 0);
			if (lsClients)
				SetFont(lsClients);
			lsFiles = CreateWindowEx(WS_EX_CLIENTEDGE, L"Listbox", L"", DefaultStyles | WS_VSCROLL, 430, 173, 420, 185, hWnd, 0, WinInstance, 0);
			if (lsFiles)
				SetFont(lsFiles);
		}
		break;
		
		case WM_CLOSE:
			DestroyWindow(hWnd);
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hWnd, Message, wParam, lParam);
	}
	return 0;
}

void AddLog(std::wstring Message)
{
	std::wstring Text = GetTime() + Message + L"\n";
	WPARAM Length = (WPARAM)GetWindowTextLength(tLog);

	if (Length > 4096)
		SendMessage(tLog, WM_SETTEXT, NULL, (LPARAM)L"");
	
	SendMessage(tLog, EM_SETSEL, Length, Length);
	SendMessage(tLog, EM_REPLACESEL, 0, (LPARAM)Text.c_str());
};

std::wstring GetTime()
{
	time_t current_time;
	tm time_info;
	char timeString[15];	// "[HH:MM:SS AMPM] \0"

	time(&current_time);
	localtime_s(&time_info, &current_time);

	strftime(timeString, sizeof(timeString), "[%I:%M:%S %p] ", &time_info);

	// Convert const char* to std::string
	std::string s_Temp(timeString);

	return std::wstring(s_Temp.begin(), s_Temp.end());
};

int FindIndex(SOCKET Socket)
{
	int ItemIndex = SendMessage(lsClients, LB_GETCOUNT, 0, 0) - 1;

	if (ItemIndex < 0)
		return 0;

	// Loop through the list in reverse until a match is found
	while (ItemIndex >= 0)
	{
		int Length = (int)SendMessage(lsClients, LB_GETTEXTLEN, (WPARAM)ItemIndex, 0);

		if (Length > 0)
		{
			// Get text of item
			std::vector<WCHAR> Buffer(Length + 1);
			SendMessage(lsClients, LB_GETTEXT, (WPARAM)ItemIndex, (LPARAM)&Buffer[0]);
			std::wstring ItemText(&Buffer[0]);

			// Add space to avoid errors
			ItemText += L" ";

			// Separate first word
			int Space = ItemText.find(L" ");

			std::string StrSocket(ItemText.begin(), ItemText.begin() + Space);

			// Convert word and SOCKET to integars to compare
			int iSocket = atoi(StrSocket.c_str());
			if (iSocket == (int)Socket)
				return ItemIndex;
		}

		--ItemIndex;
	}

	return 0;
};

bool MatchString(std::wstring First, std::wstring Second, bool CaseSensitive)
{
	if (!CaseSensitive)
	{
		std::transform(First.begin(), First.end(), First.begin(), ::tolower);
		std::transform(Second.begin(), Second.end(), Second.begin(), ::tolower);
	}

	return (First == Second);
};

void DisplayFile(std::string Filename, const int Position, const int Size)
{
	int ItemIndex = SendMessage(lsFiles, LB_GETCOUNT, 0, 0) -1;

	if (ItemIndex >= 0)
	{
		std::wstring wsFilename(Filename.begin(), Filename.end());

		// Loop through the list in reverse until a match is found
		while (ItemIndex >= 0)
		{
			int Length = (int)SendMessage(lsFiles, LB_GETTEXTLEN, (WPARAM)ItemIndex, 0);

			if (Length > 0)
			{
				// Get text of item
				std::vector<WCHAR> Buffer(Length + 1);
				SendMessage(lsFiles, LB_GETTEXT, (WPARAM)ItemIndex, (LPARAM)&Buffer[0]);
				std::wstring ItemText(&Buffer[0]);

				// If matched filename, then update file completion
				if (ItemText.substr(0, wsFilename.size()) == wsFilename)
				{
					std::wstringstream Text;
					Text << wsFilename << L" - (" << Position << " / " << Size << L") [" << (int)(Position * 100 / Size) << "% Complete]";

					SendMessage(lsFiles, LB_DELETESTRING, (WPARAM)ItemIndex, 0);
					SendMessage(lsFiles, LB_INSERTSTRING, (WPARAM)ItemIndex, (LPARAM)Text.str().c_str());

					break;
				}
			}

			--ItemIndex;
		}
	}
};

bool StartWinsock()
{
	WSADATA wsaData;

	// Start up Winsock v2.2
	if (!(WSAStartup(0x0202, &wsaData)))
		if (wsaData.wVersion != 0x0202)
			WSACleanup();
		else
			// Winsock successfully started
			return true;

	// Failed to start up winsock
	return false;
};

// Return port number listening on, return 0 if failed to listen
int StartListen(int Port)
{
	// If winsock doesn't start return false
	if (!StartWinsock())
		return 0;

	// Limit port to 1-32767 range
	if ((Port < 1) || (Port > 32767))
		Port = 80;

	SocketListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (SocketListen == INVALID_SOCKET)
	{
		SocketListen = NULL;
		return 0;
	}

	sockaddr_in SockAddr;

	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(Port);
	SockAddr.sin_addr.S_un.S_addr = INADDR_ANY;

	int Result = bind(SocketListen, (sockaddr*)(&SockAddr), sizeof(SockAddr));

	if (Result != 0)
	{
		CloseWinsock();
		SocketListen = NULL;
		return 0;
	}

	listen(SocketListen, 1);
	WSAAsyncSelect(SocketListen, fMain, WM_SOCKET, FD_CONNECT | FD_ACCEPT);

	return Port;
};

std::wstring GetLocalIP()
{
	char Hostname[128];
	
	int Return = gethostname(Hostname, 128);

	if (Return == SOCKET_ERROR)
		return NULL;

	hostent *ipHostname = gethostbyname(Hostname);

	if (ipHostname == 0)
		return NULL;

	in_addr Addr;
	memcpy(&Addr, ipHostname->h_addr_list[0], sizeof(in_addr));
	strcpy_s(Hostname, 128, inet_ntoa(Addr));

	std::string LocalIP(Hostname);
	return std::wstring(LocalIP.begin(), LocalIP.end());
};

void CloseWinsock()
{
	WSACleanup();
};