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

uint16_t base = 0;        // 基础序号
uint16_t nextSeqNum = 0;  // 下一个序列号
mutex bufferMutex;        // 缓冲区互斥锁
Windows sendBuffer;       // 发送缓冲区
FileDescriptor file;      // 文件描述符

set<pair<int, int>>
    timerArr[RTO + 1];  // 超时计时器数组， 存储着数据包在发送缓冲区的索引
int timerIndex = 0;  // 指向最新的集合，它的下一个集合为最老的集合

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
    WindowsItem* tempItem;
    set<pair<int, int>>* oldestSet;
    while (true) {
        Sleep(1);  // 休眠1毫秒

        std::lock_guard<std::mutex> lockGuard(bufferMutex);
        oldestSet = &timerArr[(timerIndex + 1) % (RTO + 1)];
        for (auto it = oldestSet->begin(); it != oldestSet->end();
             ++it) {  // 遍历最老集合的数据包
            tempItem = sendBuffer.buffer + it->first;
            if (!tempItem->isRecved &&
                it->second >= base) {  // 如果该数据包还没接收
                sendNum += 1;          // 增加发送包数量计数
                lossNum += 1;          // 增加丢失包数量计数
                timerArr[timerIndex].insert(
                    *it);  // 在最新的集合加入该数据包的索引

                // 输出日志
                if (PRINT_LOG) {
                    printf("[TIMEOUT] Package %d Timeouted.\n",
                           tempItem->message.seq);
                    tempItem->message.printMessage("[SEND]");
                }

                // 发送数据包
                sendto(serverSocket, (char*)(tempItem), sizeof(Message), 0,
                       (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));
            }
        }

        // 更新timerIndex到最新的集合并清空
        timerIndex = (timerIndex + 1) % (RTO + 1);
        timerArr[timerIndex].clear();
    }
    return 0;
}

// 线程函数，用于接收数据包
DWORD WINAPI recvThread() {
    Message tempMessage;
    WindowsItem* tempItem;
    while (true) {
        int clientAddressLength = sizeof(SOCKADDR);
        int recvLength =
            recvfrom(serverSocket, (char*)&tempMessage, sizeof(Message), 0,
                     (sockaddr*)&clientAddr, &clientAddressLength);

        // 检测是否接收成功
        if (recvLength == -1)
            exit(0);
        // 输出接收到的消息
        if (PRINT_LOG)
            tempMessage.printMessage("[RECV]");

        // 检查校验和以及是否该消息还没ACK
        tempItem = sendBuffer.getItem(tempMessage.ack);
        if (tempMessage.checksumValid(&recvPseudoHeader) && tempItem &&
            !tempItem->isRecved) {
            tempItem->isRecved = true;  // 标记该数据包已经接收

            // 将已经接收的数据包弹出，直到遇到未接收的数据包
            lock_guard<mutex> lockGuard(bufferMutex);
            while (tempItem = sendBuffer.pop()) {
                // 增加base
                base++;

                // 当前发送任务完成
                if (PRINT_LOG) {
                    printf("[WINS] : Base = %d, nextSeqNum = %d\n", base,
                           nextSeqNum);
                    printf("[SUCC] : Package (SEQ:%d) sent successfully!\n",
                           tempItem->message.seq);
                }

                // 握手成功和挥手成功的消息
                if (tempItem->message.getFlag(SYN) && PRINT_LOG) {
                    printf("[LOG] Shake hand successfully!\n");
                } else if (tempItem->message.getFlag(FIN)) {
                    if (PRINT_LOG) {
                        printf("[LOG] Wave hand successfully!\n");
                    }
                    return 0;
                }

                tempItem->isRecved = false;
            }
        } else if (PRINT_LOG) {
            printf("[IGNORE] Ignore Message of Seq = %d\n", tempMessage.ack);
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
    WindowsItem* item = sendBuffer.nextEmpty();
    Message* message = &(item->message);
    timerArr[timerIndex].insert(
        make_pair(sendBuffer.getIndex(item), nextSeqNum));  // 插入到超时队列
    // 更新消息内容，包括目的端口、源端口、序列号、标志位、数据大小、数据指针等信息
    message->update(SERVER_PORT, CLIENT_PORT, 0, nextSeqNum, flags, dataSize,
                    dataPtr, &sendPseudoHeader);

    // 通过套接字发送消息
    sendto(serverSocket, (char*)message, sizeof(Message), 0,
           (sockaddr*)&clientAddr, sizeof(SOCKADDR));

    sendNum += 1;
    nextSeqNum++;

    // 打印发送日志
    if (PRINT_LOG) {
        message->printMessage("[SEND]");
        printf("[WINS] : Base = %d, nextSeqNum = %d\n", base, nextSeqNum);
    }
}

// 发送文件的函数，根据文件名数组和数量发送文件内容
void sendFiles(const char* fileNameArr[], int size) {
    if (!PRINT_LOG) {
        printf("name\ttime(ms)\tthroughput_rate(kbps)\tloss_rate\n");
    }
    // 遍历文件名数组
    for (int i = 0; i < size; i++) {
        long long time = 0;
        float lossRate = 0;
        for (int step = 0; step < (WARM_NUM + REPEAT_NUM); step++) {
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
            sendMessage(FHD, sizeof(FileDescriptor), (char*)&file);

            // 计算需要多少个报文
            int segments = (file.fileSize + MSS - 1) / MSS;

            // 按段发送文件内容
            int len = file.fileSize;
            for (int i = 0; i < segments; i++) {
                char fileContent[MSS];
                ifs.read(fileContent, min(len, MSS));
                sendMessage(ACK, min(len, MSS), fileContent);
                len -= MSS;
            }

            // 等待发送完毕
            while (!sendBuffer.isEmpty()) {
                Sleep(1);
            }

            // 结束及时并计算耗时
            auto end = chrono::steady_clock::now();
            auto duration =
                chrono::duration_cast<chrono::milliseconds>(end - start)
                    .count();

            // 如果热身完毕，开始增加计时
            if (step >= WARM_NUM) {
                time += duration;
                lossRate += (float)lossNum / sendNum;
            }

            // 发送完后情况lossNum和sendNum
            lossNum = 0;
            sendNum = 0;
        }

        if (PRINT_LOG) {
            printf("name\ttime(ms)\tthroughput_rate(kbps)\tloss_rate\n");
        }
        // 打印文件传输的相关信息
        printf("%s\t%.3f\t%.3f\t%.3f\n", file.fileName,
               (float)time / REPEAT_NUM,
               ((float)file.fileSize * 8 * REPEAT_NUM / time),
               lossRate / REPEAT_NUM);
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

    // 如果没有参数则发送全部文件；否则，发送指定文件
    if (argc < 2) {
        sendFiles(fileNameArr, sizeof(fileNameArr) / sizeof(char*));
    } else {
        sendFiles(argv + 1, argc - 1);
    }

    // 通过两次挥手销毁连接
    sendMessage(FIN, 0, NULL);

    // 等待收到客户端的FIN|ACK信号
    WaitForSingleObject(myRecvThread, INFINITE);

    // 清理资源
    closesocket(serverSocket);
    WSACleanup();
}