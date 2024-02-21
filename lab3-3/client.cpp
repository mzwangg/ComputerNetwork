#include "Util.h"
#pragma comment(lib, "Ws2_32.lib")
using namespace std;

SOCKADDR_IN clientAddr;  // 客户端地址
SOCKADDR_IN serverAddr;  // 服务器地址
SOCKET clientSocket;     // 客户端套接字

// 定义伪首部，用于计算校验和
PseudoHeader sendPseudoHeader;
PseudoHeader recvPseudoHeader;

// 退出等待时间、开始退出标志和当前状态
int exitTime = 0;
bool beginExit = false;

// 文件目录
string fileDir = "recv";

// 接收缓冲区
Windows recvBuffer;

// 发送标志的函数，用于模拟发送带有特定标志的消息
bool sendFlag(uint16_t ack, uint16_t flags) {
    // 模拟丢包情况
    if (rand() % 100 < LOSS_RATE) {
        if (PRINT_LOG)
            printf("[LOSS] Package %d has been lost.\n", ack);
        return false;
    }

    // 构造发送缓冲区
    Message sendMessage{CLIENT_PORT, SERVER_PORT, ack, 0};
    sendMessage.setFlag(flags);
    sendMessage.setLen(0);

    // 设置校验和
    sendMessage.setChecksum(&sendPseudoHeader);

    // 打印发送的消息内容
    if (PRINT_LOG)
        sendMessage.printMessage("[SEND]");

    // 发送消息到服务器
    sendto(clientSocket, (char*)&sendMessage, sizeof(Message), 0,
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

    if (_access(fileDir.c_str(), 0) == -1)  // 如果文件夹不存在
        _mkdir(fileDir.c_str());            // 则创建
}

DWORD WINAPI waitExit(LPVOID lpParam) {
    // 循环等待退出时间达到指定条件
    while (true) {
        Sleep(1);    // 暂停线程执行
        exitTime++;  // 增加退出计时器

        // 如果退出计时器达到两倍的超时时间
        if (exitTime >= 2 * RTO) {
            // 执行退出操作
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
    Message tempMessage;
    WindowsItem* tempItemPtr;
    Message* tempMessagePtr;

    // 写入文件
    ofstream ofs;
    unsigned int fileSize = 0xffffffff;
    unsigned int currentSize = 0;
    string filename;

    while (true) {
        // 从客户端套接字接收消息
        int serverAddressLength = sizeof(SOCKADDR);
        int recvLength =
            recvfrom(clientSocket, (char*)&tempMessage, sizeof(Message), 0,
                     (sockaddr*)&serverAddr, &serverAddressLength);

        // 检测是否接收成功
        if (recvLength == -1) {
            printf("exit!\n");
            exit(0);
        }
        // 打印接收到的消息内容
        if (PRINT_LOG)
            tempMessage.printMessage("[RECV]");

        // 校验和正确，且该消息能放在当前窗口，且还没被接收
        tempItemPtr = recvBuffer.getItem(tempMessage.seq);
        if (tempMessage.checksumValid(&recvPseudoHeader) && tempItemPtr &&
            !tempItemPtr->isRecved) {
            // 将接收到的消息加入到接收缓冲区
            tempItemPtr->isRecved = true;
            tempItemPtr->message = tempMessage;

            // 不断pop出接收缓冲区中最前面的消息，直到还没有接收到该消息
            while (tempItemPtr = recvBuffer.pop()) {
                // 从接收缓冲区中取出消息
                tempMessagePtr = &(tempItemPtr->message);
                tempItemPtr->isRecved = false;

                // 如果消息是FIN
                if (tempMessagePtr->getFlag(FIN)) {  // 如果消息是FIN
                    // 向服务器发送ACK
                    sendFlag(tempMessagePtr->seq, ACK);
                    exitTime = 0;
                    // 等待退出
                    if (!beginExit) {
                        beginExit = true;
                        HANDLE myWaitThread =
                            CreateThread(NULL, 0, waitExit, NULL, 0, 0);
                    }
                } else if (!tempMessagePtr->getFlag(SYN)) {  // 如果不是SYN消息
                    // 如果消息包含文件头
                    if (tempMessagePtr->getFlag(FHD)) {
                        // 从消息中提取文件描述符
                        FileDescriptor fileDescriptor;
                        memcpy(&fileDescriptor, tempMessagePtr->data,
                               sizeof(FileDescriptor));

                        // 设置文件大小和文件名
                        fileSize = fileDescriptor.fileSize;
                        filename = fileDir + "/" + fileDescriptor.fileName;
                        currentSize = 0;

                        // 创建文件
                        ofs.open(filename, ios::out | ios::binary | ios::trunc);
                    } else {
                        // 写入文件
                        ofs.write((char*)tempMessagePtr->data,
                                  tempMessagePtr->getLen());
                        currentSize += tempMessagePtr->getLen();
                    }
                }

                // 如果文件接收完成
                if (currentSize >= fileSize) {
                    currentSize = 0;
                    printf("[LOG] File receive success! %s\n",
                           filename.c_str());
                    ofs.close();
                }
            }
        }

        // 如果不是FIN，则发送ACK，表明接收到该消息
        if (tempMessage.checksumValid(&recvPseudoHeader) &&
            !tempMessagePtr->getFlag(FIN))
            sendFlag(tempMessage.seq, ACK);
    }
}

int main() {
    // 初始化客户端
    clientInit();

    // 开始接收消息
    beginRecv();
}