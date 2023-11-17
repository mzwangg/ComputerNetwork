#include "Util.h"
#pragma comment(lib, "Ws2_32.lib")
using namespace std;

// 定义客户端套接字和地址、端口信息
SOCKET clientSocket;
SOCKADDR_IN clientAddr;
const char* CLIENT_IP = "127.0.0.1";
const uint32_t CLIENT_PORT = 1234;

// 定义服务器地址和端口
SOCKADDR_IN serverAddr;
const char* SERVER_IP = "127.0.0.1";
const uint32_t SERVER_PORT = 5678;

// 定义伪首部，用于计算校验和
PseudoHeader sendPseudoHeader;
PseudoHeader recvPseudoHeader;

// 退出等待时间、开始退出标志和当前状态
int exitTime = 0;
bool beginExit = false;
uint16_t state = 0;

// 文件目录
string fileDir = "recv";

// 发送标志的函数，用于模拟发送带有特定标志的消息
bool sendFlag(uint16_t seq, uint16_t flags) {
    // 模拟丢包情况
    if (rand() % 100 < LOSS_RATE)
        return false;

    // 构造发送缓冲区
    Message sendBuffer{CLIENT_PORT, SERVER_PORT, 0, seq};
    sendBuffer.setFlag(flags);
    sendBuffer.setLen(0);

    // 设置校验和
    sendBuffer.setChecksum(&sendPseudoHeader);

    // 打印发送的消息内容
    if (PRINT_LOG) {
        printf("[SEND]");
        sendBuffer.printMessage();
    }

    // 发送消息到服务器
    sendto(clientSocket, (char*)&sendBuffer, sizeof(Message), 0,
           (sockaddr*)&serverAddr, sizeof(SOCKADDR));

    return true;
}

void clientInit() {
    // 初始化 winsock库
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Failed to initialize Winsock.\n";
        exit(1);
    }

    // 创建套接字
    clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        cerr << "Failed to create socket.\n";
        WSACleanup();
        exit(1);
    }

    // 设置客户端端口和地址
    memset(&clientAddr, 0, sizeof(clientAddr));  // 每个字节都用0填充
    clientAddr.sin_family = AF_INET;             // 使用IPv4地址
    clientAddr.sin_addr.s_addr = inet_addr(CLIENT_IP);  // 具体的IP地址
    clientAddr.sin_port = htons(CLIENT_PORT);           // 端口

    // 设置服务器端口和地址
    memset(&serverAddr, 0, sizeof(serverAddr));  // 每个字节都用0填充
    serverAddr.sin_family = AF_INET;             // 使用IPv4地址
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);  // 具体的IP地址
    serverAddr.sin_port = htons(SERVER_PORT);           // 端口

    // 将用户套接字与特定的网络地址和端口绑定在一起
    if (bind(clientSocket, (sockaddr*)&clientAddr, sizeof(clientAddr)) ==
        SOCKET_ERROR) {
        cerr << "Failed to bind socket.\n";
        closesocket(clientSocket);
        WSACleanup();
        exit(1);
    }

    // 初始化伪首部
    sendPseudoHeader.sourceIP = clientAddr.sin_addr.S_un.S_addr;
    sendPseudoHeader.destinationIP = serverAddr.sin_addr.S_un.S_addr;
    recvPseudoHeader.sourceIP = serverAddr.sin_addr.S_un.S_addr;
    recvPseudoHeader.destinationIP = clientAddr.sin_addr.S_un.S_addr;
}

DWORD WINAPI waitExit(LPVOID lpParam) {
    // 循环等待退出时间达到指定条件
    while (true) {
        Sleep(1);    // 暂停线程执行
        exitTime++;  // 增加退出计时器

        // 如果退出计时器达到两倍的超时时间
        if (exitTime >= 2 * RTO) {
            // 执行退出操作
            printf("[LOG] Wave hand successfully!\n");
            closesocket(clientSocket);  // 关闭客户端套接字
            WSACleanup();               // 清理 Winsock 资源
            exit(0);                    // 退出程序
        }
    }

    // 该部分代码不会被执行，添加是为了防止warning
    return 0;
}

void beginRecv() {
    // 接收从服务器传来的消息
    Message recvBuffer;

    // 写入文件
    ofstream ofs;
    unsigned int fileSize;
    unsigned int currentSize = 0;
    string filename;

    while (true) {
        // 从客户端套接字接收消息
        int serverAddressLength = sizeof(SOCKADDR);
        int recvLength =
            recvfrom(clientSocket, (char*)&recvBuffer, sizeof(Message), 0,
                     (sockaddr*)&serverAddr, &serverAddressLength);

        // 检测是否接收成功
        if (recvLength == -1) {
            cerr << "Error receiving data.\n";
            exit(0);
        }

        // 打印接收到的消息内容
        if (PRINT_LOG) {
            printf("[RECV]");
            recvBuffer.printMessage();
        }

        // 验证消息的校验和，且序列号与期望序列号相等
        if (recvBuffer.checksumValid(&recvPseudoHeader) &&
            recvBuffer.seq == state) {
            // 如果消息是SYN
            if (recvBuffer.getFlag(SYN)) {
                // 向服务器发送ACK和SYN
                if (sendFlag(state, ACK | SYN)) {
                    printf("[LOG] Shake hand successfully!\n");
                }
            }
            // 如果消息是FIN
            else if (recvBuffer.getFlag(FIN | ACK) == (FIN | ACK)) {
                // 向服务器发送ACK
                sendFlag(state, ACK);
                exitTime = 0;
                // 等待退出
                if (!beginExit) {
                    beginExit = true;
                    HANDLE myWaitThread =
                        CreateThread(NULL, 0, waitExit, NULL, 0, 0);
                }
            }
            // 如果消息包含数据
            else {
                // 如果消息包含文件头
                if (recvBuffer.getFlag(FHD)) {
                    // 从消息中提取文件描述符
                    FileDescriptor fileDescriptor;
                    memcpy(&fileDescriptor, recvBuffer.data,
                           sizeof(FileDescriptor));

                    // 打印接收到的文件头信息
                    printf("[LOG] Receive file header: [Name:%s] [Size:%d]\n",
                           fileDescriptor.fileName, fileDescriptor.fileSize);

                    // 设置文件大小和文件名
                    fileSize = fileDescriptor.fileSize;
                    filename = fileDir + "/" + fileDescriptor.fileName;
                    currentSize = 0;

                    // 创建文件
                    ofs.open(filename, ios::out | ios::binary | ios::trunc);
                } else {
                    // 写入文件
                    ofs.write((char*)recvBuffer.data, recvBuffer.getLen());
                    currentSize += recvBuffer.getLen();
                }

                // 向服务器发送ACK
                sendFlag(state, ACK);

                // 如果文件接收完成
                if (currentSize >= fileSize) {
                    printf("[LOG] File receive success! %s\n",
                           filename.c_str());
                    ofs.close();
                }
            }

            // 更新当前的期望序列号
            state = (state + 1) & 1;
        } else {
            // 如果数据包无效或者接收序列号与当前序列号不相等，
            // 需要重新发送上一个ACK
            // 如果消息是SYN，重新发送ACK和SYN，否则，重新发送ACK
            if (recvBuffer.getFlag(SYN)) {
                if (sendFlag((state + 1) & 1, ACK | SYN)) {
                    printf("[LOG] Shake hand successfully!\n");
                }
            } else {
                sendFlag((state + 1) & 1, ACK);
            }
        }
    }
}

int main() {
    // 初始化客户端
    clientInit();

    // 开始接收消息
    beginRecv();
}