#include "Util.h"
#pragma comment(lib, "Ws2_32.lib")
using namespace std;

SOCKADDR_IN clientAddr;               // 客户端地址
const char* CLIENT_IP = "127.0.0.1";  // 客户端 IP 地址
const uint32_t CLIENT_PORT = 1234;    // 客户端端口号

SOCKET serverSocket;                  // 服务器套接字
SOCKADDR_IN serverAddr;               // 服务器地址
const char* SERVER_IP = "127.0.0.1";  // 服务器 IP 地址
const uint32_t SERVER_PORT = 5678;    // 服务器端口号

PseudoHeader sendPseudoHeader;  // 发送伪首部
PseudoHeader recvPseudoHeader;  // 接收伪首部

bool ACK_FLAG = false;   // ACK 标志，用于表示是否收到确认
uint16_t state = 0;      // 状态
string fileDir = "res";  // 文件夹路径
char* fileNameArr[] = {"1.jpg", "2.jpg", "3.jpg",
                       "helloworld.txt"};  // 文件名数组

int sendNum = 0;  // 发送的包数量
int lossNum = 0;  // 丢失的包数量

void serverInit() {
    // 设置随机数种子
    srand(SEED);

    // 初始化 winsock库
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Failed to initialize Winsock.\n";
        exit(1);
    }

    // 创建套接字
    serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == INVALID_SOCKET) {
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
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) ==
        SOCKET_ERROR) {
        cerr << "Failed to bind socket.\n";
        closesocket(serverSocket);
        WSACleanup();
        exit(1);
    }

    // 初始化伪首部
    sendPseudoHeader.sourceIP = clientAddr.sin_addr.S_un.S_addr;
    sendPseudoHeader.destinationIP = serverAddr.sin_addr.S_un.S_addr;
    recvPseudoHeader.sourceIP = serverAddr.sin_addr.S_un.S_addr;
    recvPseudoHeader.destinationIP = clientAddr.sin_addr.S_un.S_addr;
}

DWORD WINAPI resendThread(Message* message) {
    // 如果 ACK_FLAG 没有设置，就继续计时，超时时重新发送
    int waitTime = 0;
    while (!ACK_FLAG) {
        // 等待超时时间，每次等待 1 毫秒
        while (waitTime < RTO && !ACK_FLAG) {
            Sleep(1);
            waitTime += 1;
        }
        if (!ACK_FLAG) {
            // 如果超时仍未收到确认，执行重发操作
            if (PRINT_LOG) {
                printf("[Timeout] : Resend Package\n[SEND]");
                message->printMessage();
            }

            sendNum += 1;  // 增加发送包数量计数
            waitTime = 0;  // 重置等待时间
            lossNum += 1;  // 增加丢失包数量计数

            // 模拟丢包情况
            if (rand() % 100 >= LOSS_RATE) {
                // 重新发送数据包
                sendto(serverSocket, (char*)message, sizeof(Message), 0,
                       (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));
            }
        }
    }
    return 0;
}

void sendPackage(Message message) {
    // 如果不随机丢包，发送消息到客户端
    ACK_FLAG = false;  // 设置 ACK_FLAG 为 false
    sendNum += 1;

    // 模拟丢包情况
    if (rand() % 100 >= LOSS_RATE) {
        sendto(serverSocket, (char*)&message, sizeof(Message), 0,
               (sockaddr*)&clientAddr, sizeof(SOCKADDR));
    }

    // 打印发送日志
    if (PRINT_LOG) {
        printf("[SEND]");
        message.printMessage();
    }

    // 创建重传线程
    HANDLE myResendThread = CreateThread(
        NULL, 0, (LPTHREAD_START_ROUTINE)resendThread, &message, 0, 0);

    // 然后等待 ACK
    while (true) {
        Message recvBuffer;
        int clientAddressLength = sizeof(SOCKADDR);
        int recvLength =
            recvfrom(serverSocket, (char*)&recvBuffer, sizeof(Message), 0,
                     (sockaddr*)&clientAddr, &clientAddressLength);

        // 检测是否接收成功
        if (recvLength == -1) {
            cerr << "[ERROR] : Package received from socket failed!\n";
            exit(0);
        }

        // 输出接收到的消息
        if (PRINT_LOG) {
            printf("[RECV]");
            recvBuffer.printMessage();
        }

        // 检查校验和和 ACK
        // 只有当校验和有效并且 ACK 是当前序列的，才表示数据包成功发送和接收
        if (recvBuffer.checksumValid(&recvPseudoHeader) &&
            recvBuffer.seq == state) {
            ACK_FLAG = true;

            // 等待重传线程结束
            WaitForSingleObject(myResendThread, INFINITE);

            // 更新当前序列
            state = (state + 1) & 1;

            // 当前发送任务完成
            if (PRINT_LOG) {
                printf("[LOG] : Package (SEQ:%d) sent successfully!\n",
                       recvBuffer.seq);
            }
            break;
        }
        // 如果校验和无效或 ACK 不是当前序列，表示刚刚发送的数据包失败
        // 等待超时重新发送数据包
        else {
            if (PRINT_LOG) {
                printf(
                    "[RE] : Client received failed. Wait for timeout to "
                    "resend\n");
            }
        }
    }
}

// 发送消息的函数，用于构造并发送消息
void sendMessage(uint16_t flags, uint16_t dataSize, char* dataPtr) {
    // 构建消息的发送缓冲区，初始化端口号和状态
    Message sendBuffer{SERVER_PORT, CLIENT_PORT, 0, state};
    sendBuffer.setFlag(flags);
    sendBuffer.setLen(dataSize);
    sendBuffer.setData(dataPtr);
    sendBuffer.setChecksum(&sendPseudoHeader);

    // 调用发送消息的函数
    sendPackage(sendBuffer);
}

// 建立连接的函数，发送一个带有 SYN 标志的消息
void establishConnection() {
    sendMessage(SYN, 0, NULL);
    printf("[LOG] Shake hand successfully!\n");
}

// 发送文件的函数，根据文件名数组和数量发送文件内容
void sendFiles(char* fileNameArr[], int size) {
    // 遍历文件名数组
    for (int i = 0; i < size; i++) {
        // 开始计时
        auto start = chrono::steady_clock::now();

        // 创建文件描述符对象，存储文件名和大小
        FileDescriptor file;
        strcpy(file.fileName, fileNameArr[i]);

        // 打开文件并获取文件大小
        ifstream ifs(fileDir + "/" + file.fileName,
                     ios::binary | ios::ate | ios::app);
        file.fileSize = ifs.tellg();
        ifs.seekg(0, ios::beg);

        // 发送包含文件名和大小的文件描述消息
        sendMessage(ACK | FHD, sizeof(FileDescriptor), (char*)&file);

        // 计算需要多少个报文
        int segments = (file.fileSize + MSS - 1) / MSS;

        // 按段发送文件内容
        int len = file.fileSize;
        for (int i = 0; i < segments; i++) {
            char fileContent[MSS];
            ifs.read(fileContent, min(len, MSS));
            sendMessage(ACK, min(len, MSS), fileContent);
            len -= MSS;

            // 打印日志
            if (PRINT_LOG) {
                printf("[Seg %d in %d]\n", i, segments - 1);
            }
        }

        // 结束及时并计算耗时
        auto end = chrono::steady_clock::now();
        auto duration =
            chrono::duration_cast<chrono::milliseconds>(end - start).count();

        // 打印文件传输的相关信息
        printf(
            "fileName:%s, fileSize:%dbytes, duration:%dms,\n"
            "Throughput Rate:%.5fkbps, Packet Loss Rate:%.5f\n",
            file.fileName, file.fileSize, duration,
            ((float)file.fileSize * 8 / duration), (float)lossNum / sendNum);
        lossNum = 0;
        sendNum = 0;
    }
}

// 销毁连接的函数，发送一个带有 FIN 标志的消息
void destroyConnection() {
    sendMessage(FIN | ACK, 0, NULL);
    printf("[LOG] Wave hand successfully!\n");
}

int main(int argc, char* argv[]) {
    // 初始化服务器设置
    serverInit();

    // 通过两次握手建立连接
    establishConnection();

    // 如果没有参数或参数为 "all" 时，发送全部文件；否则，发送指定文件
    if (argc < 2 || strcmp(argv[1], "all") == 0) {
        sendFiles(fileNameArr, 4);
    } else {
        sendFiles(argv + 1, argc - 1);
    }

    // 通过两次挥手销毁连接
    destroyConnection();

    // 清理资源
    closesocket(serverSocket);
    WSACleanup();
}
