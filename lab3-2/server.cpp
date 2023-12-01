#include "Util.h"
#pragma comment(lib, "Ws2_32.lib")
using namespace std;

SOCKADDR_IN clientAddr;  // 客户端地址
SOCKADDR_IN serverAddr;  // 服务器地址
SOCKET serverSocket;     // 服务器套接字

PseudoHeader sendPseudoHeader;  // 发送伪首部
PseudoHeader recvPseudoHeader;  // 接收伪首部

string fileDir = "res";  // 文件夹路径
const char* fileNameArr[] = {"1.jpg", "2.jpg", "3.jpg",
                             "helloworld.txt"};  // 文件名数组

int sendNum = 0;  // 发送的包数量
int lossNum = 0;  // 丢失的包数量

int waitTime = 0;         // 等待时间
bool timerValid = false;  // 定时器有效标志
uint16_t base = 0;        // 基础序号
uint16_t nextSeqNum = 0;  // 下一个序列号
mutex bufferMutex;        // 缓冲区互斥锁
SendBuffer sendBuffer;    // 发送缓冲区
FileDescriptor file;      // 文件描述符

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

// 线程函数，用于重新发送超时的数据包
DWORD WINAPI resendThread() {
    while (true) {
        Sleep(1);  // 休眠1毫秒

        // 当定时器无效时，继续循环
        if (!timerValid)
            continue;

        // 当等待时间小于重传超时时，增加等待时间并继续循环
        waitTime++;
        if (waitTime < RTO)
            continue;

        waitTime = 0;  // 重置等待时间
        sendNum += 1;  // 增加发送包数量计数
        lossNum += 1;  // 增加丢失包数量计数

        std::lock_guard<std::mutex> lockGuard(
            bufferMutex);  // 使用互斥锁保护发送缓冲区
        if (PRINT_LOG) {
            printf("[Timeout] : Package From %d to %d Resend!\n", base,
                   prevSeq(nextSeqNum));
        }
        for (int i = sendBuffer.begin; i != sendBuffer.end;
             i = sendBuffer.nextIndex(i)) {
            // 打印发送日志
            if (PRINT_LOG) {
                printf("[SEND]");
                (sendBuffer.buffer + i)->printMessage();
            }

            // 模拟丢包情况
            if (rand() % 100 < LOSS_RATE)
                continue;

            // 发送数据包
            sendto(serverSocket, (char*)(sendBuffer.buffer + i),
                   sizeof(Message), 0, (sockaddr*)&clientAddr,
                   sizeof(SOCKADDR_IN));
        }
    }
    return 0;
}

// 线程函数，用于接收数据包
DWORD WINAPI recvThread() {
    // 等待接收 ACK 的消息
    Message recvBuffer;
    while (true) {
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

        // 检查校验和
        if (recvBuffer.checksumValid(&recvPseudoHeader)) {
            std::lock_guard<std::mutex> lockGuard(bufferMutex);
            // 弹出已确认的数据包，更新 base
            for (int i = prevSeq(base); i != recvBuffer.ack; i = nextSeq(i)) {
                sendBuffer.pop();
                base = nextSeq(base);

                // 更新定时器状态
                waitTime = 0;
                timerValid = (base != nextSeqNum);
            }

            // 当前发送任务完成
            if (PRINT_LOG) {
                printf("[WINS] : Base = %d, nextSeqNum = %d\n", base,
                       nextSeqNum);
                printf("[SUCC] : Package (SEQ:%d) sent successfully!\n",
                       recvBuffer.ack);
            }

            // 握手成功和挥手成功的消息
            if ((recvBuffer.getFlag(SYN | ACK)) == (SYN | ACK)) {
                printf("[LOG] Shake hand successfully!\n");
            } else if ((recvBuffer.getFlag(FIN | ACK)) == (FIN | ACK)) {
                printf("[LOG] Wave hand successfully!\n");
                return 0;
            }

        } else {  // 如果校验和错误，则等待超时重新发送数据包
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
    // 等待发送缓冲区有空余位置
    while (sendBuffer.isFull()) {
        Sleep(1);
    }

    // 构建消息的发送缓冲区
    lock_guard<mutex> lockGuard(bufferMutex);
    Message* message = sendBuffer.nextEmpty();
    // 更新消息内容，包括目的端口、源端口、序列号、标志位、数据大小、数据指针等信息
    message->update(SERVER_PORT, CLIENT_PORT, 0, nextSeqNum, flags, dataSize,
                    dataPtr, &sendPseudoHeader);

    // 模拟丢包情况
    if (rand() % 100 >= LOSS_RATE) {
        // 通过套接字发送消息
        sendto(serverSocket, (char*)message, sizeof(Message), 0,
               (sockaddr*)&clientAddr, sizeof(SOCKADDR));
    }

    // 更新基准序列号和等待时间
    if (base == nextSeqNum) {
        waitTime = 0;
        timerValid = true;
    }
    sendNum += 1;
    nextSeqNum = nextSeq(nextSeqNum);

    // 打印发送日志
    if (PRINT_LOG) {
        printf("[SEND]");
        message->printMessage();
        printf("[WINS] : Base = %d, nextSeqNum = %d\n", base, nextSeqNum);
    }
}

// 发送文件的函数，根据文件名数组和数量发送文件内容
void sendFiles(const char* fileNameArr[], int size) {
    // 遍历文件名数组
    for (int i = 0; i < size; i++) {
        // 更新文件名和大小
        strcpy(file.fileName, fileNameArr[i]);

        // 打开文件并获取文件大小
        ifstream ifs(fileDir + "/" + file.fileName,
                     ios::binary | ios::ate | ios::app);
        file.fileSize = ifs.tellg();
        ifs.seekg(0, ios::beg);

        // 开始计时
        auto start = chrono::steady_clock::now();

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
                printf("[SEG] Seg %d / %d Sended\n", i, segments - 1);
            }
        }

        // 等待发送完毕
        while (!sendBuffer.isEmpty()) {
            Sleep(1);
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

int main(int argc, const char* argv[]) {
    // 初始化服务器设置
    serverInit();

    // 创建接收消息线程
    HANDLE myRecvThread =
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)recvThread, NULL, 0, 0);

    // 创建重发消息线程
    HANDLE myResendThread =
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)resendThread, NULL, 0, 0);

    // 通过两次握手建立连接
    sendMessage(SYN, 0, NULL);

    // 如果没有参数或参数为 "all" 时，发送全部文件；否则，发送指定文件
    if (argc < 2 || strcmp(argv[1], "all") == 0) {
        sendFiles(fileNameArr, 4);
    } else {
        sendFiles(argv + 1, argc - 1);
    }

    // 通过两次挥手销毁连接
    sendMessage(FIN | ACK, 0, NULL);

    // 等待收到客户端的FIN|ACK信号
    WaitForSingleObject(myRecvThread, INFINITE);

    // 清理资源
    closesocket(serverSocket);
    WSACleanup();
}