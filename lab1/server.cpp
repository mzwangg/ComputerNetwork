#include <ctime>
#include <fstream>
#include <iostream>
#include <winsock2.h>
using namespace std;
#pragma comment(lib, "ws2_32.lib")

// 定义一些参数
const int NAME_SIZE = 64;
const int CLIENT_SIZE = 256; // 队列最长为5人
const int BUFF_SIZE = 1024;
const int SERVER_PORT = 5678;
const char *SERVER_IP = "127.0.0.1";

// 定义一些变量
char time_str[20];           // 时间字符串
HANDLE Hthread[CLIENT_SIZE]; // 用于接收客户消息的线程数组
char recvBuff[BUFF_SIZE];    // 接受消息缓冲区
char sendBuff[BUFF_SIZE];    // 发送消息缓冲区
SOCKET server_socket;        // 服务器套接字
int client_num;              // 用户数，用于生成id
int client_addr_len = sizeof(SOCKADDR_IN);

struct Client {
    int id;                  // 客户端id
    int flag = 0;            // 表明该用户端是否有效
    char name[NAME_SIZE];    // 名称
    SOCKET client_socket;    // 客户端套接字
    SOCKADDR_IN client_addr; // 客户端地址
} clients[CLIENT_SIZE];

// 获得当前时间，格式为 %Y-%m-%d %H:%M:%S
char *get_time()
{
    // 获取当前时间
    time_t currentTime = time(nullptr);
    // 创建一个tm结构体来存储时间信息
    tm timeInfo = *localtime(&currentTime);
    // 使用strftime将时间格式化为需要的格式
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return time_str;
}

void set_property(uint8_t type, uint8_t send_id, uint8_t recv_id)
{
    sendBuff[0] = (uint8_t)type;
    sendBuff[1] = (uint8_t)send_id;
    sendBuff[2] = (uint8_t)recv_id;
}

DWORD WINAPI client_thread(Client *now_client)
{
    // 为了防止多用户时冲突，故新申请局部变量
    char local_recvBuff[BUFF_SIZE]; // 接受消息缓冲区
    char local_sendBuff[BUFF_SIZE]; // 发送消息缓冲区
    int receive_len;

    while (true) {
        // 等待并接受用户请求
        memset(local_recvBuff, 0, BUFF_SIZE); // 清空local_recvBuff
        receive_len = recv(now_client->client_socket, local_recvBuff, BUFF_SIZE, 0);
        if (receive_len == 0 || receive_len == -1) {
            // 此时说明该用户已经退出，可结束循环
            now_client->flag = 0;
            break;
        }
        memcpy(local_sendBuff, local_recvBuff, BUFF_SIZE);

        // 根据消息类型分别进行处理
        if (local_recvBuff[0] == (uint8_t)0) {
            // 设置消息格式
            sprintf(local_sendBuff + 22, "%.19s id:%u name:%s\n%s\n", local_recvBuff + 3, now_client->id, now_client->name, local_recvBuff + 22);
        } else if (local_recvBuff[0] == (uint8_t)2) {
            // 设置消息格式
            sprintf(local_sendBuff + 22, "%.19s id:%u name:%s quit\n", local_recvBuff + 3, now_client->id, now_client->name);
        } else if (local_recvBuff[0] == (uint8_t)3) {
            // 设置消息格式
            sprintf(local_sendBuff + 22, "%.19s id:%u name userlist:\n", local_recvBuff + 3, now_client->id, now_client->name);
            for (int i = 0; i < CLIENT_SIZE; i++) { // 不断循环找到所有在线用户
                if (clients[i].flag == 1) {
                    sprintf(local_recvBuff + 22, "id:%u name:%s\n", clients[i].id, clients[i].name);
                    strcat(local_sendBuff + 22, local_recvBuff + 22); // 将在线用户信息拼接到local_sendBuff
                }
            }
            // 单独发送到请求客户
            send(now_client->client_socket, local_sendBuff, BUFF_SIZE, 0);
        } else if (local_recvBuff[0] == (uint8_t)4) {
            // 计算要发送的id
            int send_id = local_sendBuff[2] - 1;
            // 设置消息格式
            sprintf(local_sendBuff + 22, "%.19s id:%u name:%s to id:%u name:%s\n%s\n", local_recvBuff + 3, now_client->id, now_client->name, clients[send_id].id, clients[send_id].name, local_recvBuff + 22);
            // 将信息发给对方、自己
            send(clients[send_id].client_socket, local_sendBuff, BUFF_SIZE, 0);
            send(now_client->client_socket, local_sendBuff, BUFF_SIZE, 0);
            // 在服务器中记录
            printf("%s", local_sendBuff + 22);
        } else if (local_recvBuff[0] == (uint8_t)5) {
            // 通过ifstream读取history.txt
            ifstream ifs("history.txt");
            ifs.read(local_sendBuff + 22, BUFF_SIZE - 22);
            // 将消息发送给请求客户
            send(now_client->client_socket, local_sendBuff, BUFF_SIZE, 0);
        }

        // 如果消息类型小于3，说明是需要广播到所有用户的消息
        if (local_sendBuff[0] < (uint8_t)3) {
            for (int i = 0; i < CLIENT_SIZE; i++) {
                if (clients[i].flag == 1) {
                    send(clients[i].client_socket, local_sendBuff, BUFF_SIZE, 0);
                }
            }

            // 在服务器中输出并保存到history.txt
            printf("%s", local_sendBuff + 22); // 在服务器中记录
            ofstream ofs("history.txt", ios::app | ios::out);
            ofs << local_sendBuff + 22; // 在history.txt中记录
            ofs.close();
        }
    }

    // 关闭套接字
    closesocket(now_client->client_socket);
    return 0;
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
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        cerr << "Failed to create socket.\n";
        WSACleanup();
        return 1;
    }

    // 设置服务器地址和端口
    SOCKADDR_IN serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));        // 每个字节都用0填充
    serverAddr.sin_family = AF_INET;                   // 使用IPv4地址
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP); // 具体的IP地址
    serverAddr.sin_port = htons(SERVER_PORT);          // 端口

    // 将套接字与特定的网络地址和端口绑定在一起
    if (bind(server_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Failed to bind socket.\n";
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // 监听套接字，以等待客户端连接请求
    if (listen(server_socket, CLIENT_SIZE) == SOCKET_ERROR) {
        std::cerr << "Failed to listen for connections." << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    cout << "Server is listening for connections...\n";

    // 将history.txt清空
    ofstream ofs("history.txt", ios::trunc | ios::out);
    ofs.close();

    // 主线程用于处理用户的接入请求
    while (true) {
        for (int i = 0; i < CLIENT_SIZE; i++) { // 选择一个空闲id分配给客户
            if (clients[i].flag == 0) {
                // 等待客户端请求并接受，设置一些客户端属性
                clients[i].client_socket = accept(server_socket, (SOCKADDR *)&clients[i].client_addr, &client_addr_len);
                clients[i].flag = 1;
                clients[i].id = i + 1;

                // 接收客户端发送的设置名称消息，设置名称并编写返回消息
                recv(clients[i].client_socket, recvBuff, BUFF_SIZE, 0);
                strcpy(clients[i].name, recvBuff + 22);
                set_property(1, 0, clients[i].id);
                sprintf(sendBuff + 3, "%.19s%.19s New client join, id: %d, name: %s\n", recvBuff + 3, recvBuff + 3, clients[i].id, clients[i].name);

                // 创建一个线程用于接收该客户的消息
                CloseHandle(Hthread[i]);
                Hthread[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)client_thread, &clients[i], 0, 0);

                // 将该客户加入的消息发送给所有客户
                for (int i = 0; i < CLIENT_SIZE; i++) {
                    if (clients[i].flag == 1) {
                        send(clients[i].client_socket, sendBuff, BUFF_SIZE, 0);
                    }
                }
            }
        }
    }

    // 结束
    closesocket(server_socket);
    WSACleanup();
    ofs.close();
    return 0;
}