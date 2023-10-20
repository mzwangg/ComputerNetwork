#include <ctime>
#include <iostream>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

// 定义一些参数
const int NAME_SIZE = 64;
const int BUFF_SIZE = 1024;
const int SERVER_PORT = 5678;
const char *SERVER_IP = "127.0.0.1";

// 定义一些变量
uint8_t id;               // 用户id
SOCKET client_socket;     // 用户套接字
char recvBuff[BUFF_SIZE]; // 接受消息缓冲区
char sendBuff[BUFF_SIZE]; // 发送消息缓冲区
char tempBuff[BUFF_SIZE]; // 输出消息缓冲区
char name[NAME_SIZE];     // 用于缓存姓名
char time_str[20];        // 用于保存时间

// 获得当前时间，格式为 %Y-%m-%d %H:%M:%S
char *get_time()
{
    // 获取当前时间
    time_t currentTime = time(nullptr);
    // 创建一个tm结构体来存储时间信息
    tm timeInfo = *localtime(&currentTime);
    // 使用strftime将时间格式化为您需要的格式
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return time_str;
}

// 设置属性
void set_property(uint8_t type, uint8_t send_id, uint8_t recv_id)
{
    sendBuff[0] = (uint8_t)type;
    sendBuff[1] = (uint8_t)send_id;
    sendBuff[2] = (uint8_t)recv_id;
}

// 客户端接受信息
// 该函数简单地输出服务器的响应即可
DWORD WINAPI recvMessage(LPVOID lpParam)
{
    while (true) {
        // 清空缓冲区
        memset(recvBuff, 0, BUFF_SIZE);
        recv(client_socket, recvBuff, BUFF_SIZE, 0);
        printf(recvBuff + 22);
    }
    return 0;
}

// 输出帮助信息
void help_message()
{
    cout << "you can use some commands as below:\n";
    cout << "-\t/quit\tQuit chat room\n";
    cout << "-\t/help\tShow the help list\n";
    cout << "-\t/userlist\tGet a list of online users\n";
    cout << "-\t/to userid:\tHave a private chat with someone\n";
    cout << "-\t/history:\tShow the chat history\n";
}

int main()
{
    // 初始化 winsock库
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Failed to initialize Winsock.\n";
        return 1;
    }

    // 创建套接字
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        cerr << "Failed to create socket.\n";
        WSACleanup();
        return 1;
    }

    // 设置服务器端口和地址
    SOCKADDR_IN serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));        // 每个字节都用0填充
    serverAddr.sin_family = AF_INET;                   // 使用IPv4地址
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP); // 具体的IP地址
    serverAddr.sin_port = htons(SERVER_PORT);          // 端口

    // 连接远程服务器
    if (connect(client_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Failed to connect to the server.\n";
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    // 输入姓名
    printf("please input your name:\n");
    cin.getline(name, NAME_SIZE, '\n');

    // 设置姓名与获取id
    // type为1表示设置姓名，由于此时还没有id，用0表示id没确定
    set_property(1, 0, 0);
    sprintf(sendBuff + 3, "%.19s%s", get_time(), name);
    send(client_socket, sendBuff, BUFF_SIZE, 0);
    recv(client_socket, recvBuff, BUFF_SIZE, 0);
    id = (uint8_t)recvBuff[2]; // 在响应中提取出id

    // 输出提示信息
    printf("Connect successed! User id:%u\n", id);
    help_message(); // 输出指令列表

    // 创建线程，用于接受消息：
    HANDLE recv_thread = CreateThread(NULL, 0, recvMessage, NULL, 0, 0);

    // 主线程用于发送消息
    while (true) {
        cin.getline(tempBuff, BUFF_SIZE, '\n');

        // 判断消息的类型，然后分别进行处理
        if (tempBuff[0] == '/') { // 指令消息
            if (strcmp(tempBuff + 1, "help") == 0) {
                help_message();
            } else if (strcmp(tempBuff + 1, "quit") == 0) {
                set_property(2, id, 0);
                sprintf(sendBuff + 3, "%s", get_time());
                send(client_socket, sendBuff, BUFF_SIZE, 0);
                cout << "Quit successed!\n";
                break;
            } else if (strcmp(tempBuff + 1, "userlist") == 0) {
                set_property(3, id, 0);
                sprintf(sendBuff + 3, "%s", get_time());
                send(client_socket, sendBuff, BUFF_SIZE, 0);
            } else if (tempBuff[1] == 't' && tempBuff[2] == 'o') {
                set_property(4, id, atoi(tempBuff + 4));
                sprintf(sendBuff + 3, "%s%s", get_time(), strchr(tempBuff, ':') + 1);
                send(client_socket, sendBuff, BUFF_SIZE, 0);
            } else if (strcmp(tempBuff + 1, "history") == 0) {
                set_property(5, id, 0);
                sprintf(sendBuff + 3, "%s", get_time());
                send(client_socket, sendBuff, BUFF_SIZE, 0);
            } else {
                cout << "Wrong instruction!\n";
            }
        } else { // 普通消息
            set_property(0, id, 0);
            sprintf(sendBuff + 3, "%.19s%s", get_time(), tempBuff);
            send(client_socket, sendBuff, BUFF_SIZE, 0);
        }
    }

    // 结束
    closesocket(client_socket);
    WSACleanup();
    return 0;
}