#include <iostream>
#include <mysql.h>
#include <winsock.h>
#include <process.h>

#pragma comment(lib, "libmySQL.lib")
#pragma comment(lib,"ws2_32.lib")	//ws2_32.lib 파일경로 명시, 속성창에서 라이브러리 추가하는것과 유사함

#define MAX_PACKETLEN 256	//패킷 길이
#define PORT 3500	//포트번호
#define MAX_CLNT 256	//보관가능한 최대 클라이언트 갯수

#define DB_HOST "127.0.0.1"
#define DB_USER "root"
#define DB_PASS "__" //데이터베이스 비밀번호입력
#define DB_NAME "__"	//데이터베이스 이름 입력

#define SQL_SELECT "SELECT * FROM login_table"

struct Info {
	int _order;	//1 로그인 요청, 2 회원가입 요청,  -1 로그인실패, -2 회원가입 실패
	char _name[20];
	char _pwd[20];
};

int clientCount = 0;	//현제까지 접속한 클라이언트 갯수
SOCKET clientSocks[MAX_CLNT];//클라이언트 소켓 보관용 배열
HANDLE hMutex;//뮤텍스
MYSQL* connection = NULL;
//SignUp
void SignUp(void* data, Info* tempinfoRecv, MYSQL* _connection) {
	SOCKET sockfd = *((SOCKET*)data);

	MYSQL_RES* sql_result;	//쿼리성공시 결과담는 구조체 포인터
	MYSQL_ROW sql_row;	//쿼리성공시 결과로 나온행의 정보 저장
	int query_stat;	//쿼리요청결과
	char tempSQL_insert[1024];

	Info tempinfoSend;	//데이터를 보낼 구조체변수
	bool is_same = true;

	tempinfoSend._order = -2;	//기본값은 실패로 둔다

		//동일닉네임이있는지 체크
	query_stat = mysql_query(_connection, SQL_SELECT);
	sql_result = mysql_store_result(_connection);

	while ((sql_row = mysql_fetch_row(sql_result)) != NULL)
	{
		if (strcmp(sql_row[1], tempinfoRecv->_name) == 0) {
			is_same = false;
			break;
		}
	}
	//없을시 데이터베이스에 추가
	if (is_same) {
		tempinfoSend._order = 2;
		sprintf(tempSQL_insert, "INSERT INTO login_table (name,pwd) VALUES('%s',%s)", tempinfoRecv->_name, tempinfoRecv->_pwd);
		query_stat = mysql_query(_connection, tempSQL_insert);
		send(sockfd, (char*)&tempinfoSend, sizeof(Info), 0);	//결과보내기, 가입 성공 여부를 보낸다 성공시 2 실패시 -2
	}
	mysql_free_result(sql_result);

}
//Login
void Login(void* data, Info* tempinfoRecv, MYSQL* _connection) {
	SOCKET sockfd = *((SOCKET*)data);

	MYSQL_RES* sql_result;	//쿼리성공시 결과담는 구조체 포인터
	MYSQL_ROW sql_row;	//쿼리성공시 결과로 나온행의 정보 저장
	int query_stat;	//쿼리요청결과

	Info tempinfoSend;	//데이터를 보낼 구조체변수
	tempinfoSend._order = -1;	//기본값은 실패로 둔다

	query_stat = mysql_query(_connection, SQL_SELECT);
	sql_result = mysql_store_result(_connection);
	while ((sql_row = mysql_fetch_row(sql_result)) != NULL)
	{
		if ((strcmp(sql_row[1], tempinfoRecv->_name) == 0) && (strcmp(sql_row[2], tempinfoRecv->_pwd) == 0)) {
			//데이터베이스의 값과 동일하여 로그인에 성공했을시 _order을 1로변경
			tempinfoSend._order = 1;
			printf("Succee login \n");
			break;
		}
	}
	send(sockfd, (char*)&tempinfoSend, sizeof(Info), 0);	//결과보내기, 로그인 성공 여부를 보낸다 성공시 1 실패시 -1
	mysql_free_result(sql_result);

}
unsigned WINAPI t_process(void* data) {
	SOCKET sockfd = *((SOCKET*)data);
	int strLen;	//받은메세지 길이 보관
	char szReceiveBuffer[MAX_PACKETLEN];//메세지 받을 버퍼
	Info* tempinfoRecv;

	while ((strLen = recv(sockfd, szReceiveBuffer, sizeof(szReceiveBuffer), 0)) != 0) {
		if (strLen < 0) {
			break;
		}
		tempinfoRecv = (Info*)szReceiveBuffer;
		if (tempinfoRecv->_order == 1) {
			Login(data, tempinfoRecv, connection);
		}
		else if (tempinfoRecv->_order == 2) {
			SignUp(data, tempinfoRecv, connection);
		}
	}

	WaitForSingleObject(hMutex, INFINITE);
	for (int i = 0; i < clientCount; i++) {
		if (sockfd == clientSocks[i]) {
			if (i == MAX_CLNT - 1) {
				clientSocks[i] = NULL;
				break;
			}
			while (i++ < clientCount - 1) {
				clientSocks[i] = clientSocks[i + 1];
			}
			break;
		}
	}
	clientCount--;//클라이언트 개수 하나 감소
	ReleaseMutex(hMutex);//뮤텍스 중지

	printf("Thread Close : %d\n", GetCurrentThreadId());

	closesocket(sockfd);
	return 0;
}

int main()
{
	//소켓프로그래밍 변수
	WSADATA wsaData;
	SOCKET listen_s, client_s;
	struct sockaddr_in server_addr, client_addr;
	HANDLE hTreed;	
	int addr_len;	//주소길이
	//mySQL프로그래밍 변수
	MYSQL  conn;		//conn 정보를 담는 구조체

	//소켓프로그래밍작업
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		return 1;
	}
	listen_s = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_s == INVALID_SOCKET) {
		return 1;
	}
	ZeroMemory(&server_addr, sizeof(struct sockaddr_in));	//서버주소 구조체 초기화

	server_addr.sin_family = PF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	if (bind(listen_s, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
		return 0;
	}
	if (listen(listen_s, 10) == SOCKET_ERROR) {
		return 0;
	}

	//mySQL 작업
	mysql_init(&conn);	//초기화작업
	connection = mysql_real_connect(&conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 3306, (char*)NULL, 0);	//SQL연결
	if (connection == NULL) {	
		fprintf(stderr, "connection error %s", mysql_error(&conn));
		return 1;
	}

	while (1) {
		addr_len = sizeof(client_addr);
		client_s = accept(listen_s, (SOCKADDR*)&client_addr, &addr_len);//서버에게 전달된 클라이언트 소켓 연결 허용하고 clientSock에 전달
		hTreed = (HANDLE)_beginthreadex(NULL, 0, t_process, (void*)&client_s, 0, NULL);// 쓰레드 실행
		printf("Connected Client IP : %s\n", inet_ntoa(client_addr.sin_addr));
	}
	mysql_close(connection);
	return 0;
}