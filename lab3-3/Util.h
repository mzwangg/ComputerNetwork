// 防止头文件重复包含
#ifndef LAB3_UTIL_H
#define LAB3_UTIL_H

#include <direct.h>
#include <io.h>
#include <stdint.h>
#include <winsock.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
using namespace std;

// 参数宏定义
#ifndef MAKEFILE
#define MSS 2000     // 最大报文段长度
#define LOSS_RATE 2  // 丢包率
#define RTO 2        // 等待时间
#define WIN_SIZE 32  // 窗口大小
#endif

#define SEED 0                   // 随机数种子
#define MAX_SIZE (WIN_SIZE + 1)  // 循环队列大小
#define PRINT_LOG 0              // 是否打印日志
#define WARM_NUM 0               // 热身次数
#define REPEAT_NUM 1             // 重复计时次数

const char* CLIENT_IP = "127.0.0.1";  // 客户端 IP 地址
const uint32_t CLIENT_PORT = 1234;    // 客户端端口号
const char* SERVER_IP = "127.0.0.1";  // 服务器 IP 地址
const uint32_t SERVER_PORT = 5678;    // 服务器端口号

// 标志位
const uint16_t ACK = 0x8000;
const uint16_t SYN = 0x4000;
const uint16_t FIN = 0x2000;
const uint16_t FHD = 0x1000;

// 伪首部结构体，用于校验和计算
class PseudoHeader {
public:
    uint32_t sourceIP;       // 源IP
    uint32_t destinationIP;  // 目的IP
    uint8_t zero;            // 0
    uint8_t protocol;        // 协议
    uint16_t length;         // 报文长度

    // 默认构造函数
    PseudoHeader();
};

// 文件描述符结构体，用于描述文件信息
struct FileDescriptor {
    char fileName[20];  // 文件名
    int fileSize;       // 文件大小
};

// 消息结构体，用于通信
class Message {
public:
    // 成员变量
    uint16_t sourcePort;         // 源端口
    uint16_t destinationPort;    // 目的端口
    uint16_t ack;                // ack
    uint16_t seq;                // seq
    uint16_t flagAndLength = 0;  // ACK(15)|SYN(14)|FIN(13)|FHD(12)|LEN(11-0)
    uint16_t checksum;           // 校验和
    uint8_t data[MSS];           // 数据段

    // 成员函数
    // 获取指定标志位
    uint16_t getFlag(uint16_t flag) { return flagAndLength & flag; }

    // 设置指定标志位
    void setFlag(uint16_t flag) { flagAndLength = (flag | getLen()); }

    // 获取报文段长度
    uint16_t getLen() { return flagAndLength & 0x0FFF; }

    // 设置报文段长度
    void setLen(uint16_t len) {
        flagAndLength = (flagAndLength & 0xF000) | (len & 0x0FFF);
    }

    // 设置消息数据
    void setData(char* sourceData) { memcpy(data, sourceData, getLen()); }

    // 设置校验和
    void setChecksum(PseudoHeader* pseudoHeader) {
        // 将checksum清零并计算校验和
        checksum = 0;
        checksum = ~calChecksum(pseudoHeader);
    }

    // 判断校验和是否有效
    bool checksumValid(PseudoHeader* pseudoHeader) {
        // 判断校验和是否为0xFFFF
        return calChecksum(pseudoHeader) == 0xFFFF;
    }

    void update(uint16_t sourcePort,
                uint16_t destinationPort,
                uint16_t ack,
                uint16_t seq,
                uint16_t flags,
                uint16_t dataSize,
                char* dataPtr,
                PseudoHeader* sendPseudoHeader) {
        this->sourcePort = sourcePort;
        this->destinationPort = destinationPort;
        this->ack = ack;
        this->seq = seq;
        setFlag(flags);
        setLen(dataSize);
        setData(dataPtr);
        setChecksum(sendPseudoHeader);
    }

    // 校验和计算
    uint16_t calChecksum(PseudoHeader* pseudoHeader);

    // 打印消息信息
    void printMessage(const char* typeStr);
};

// 伪首部构造函数，初始化协议相关字段
PseudoHeader::PseudoHeader() {
    this->zero = 0;
    this->protocol = 13;
    this->length = sizeof(Message);  // 伪首部长度为消息结构体的大小
}

struct WindowsItem {
    Message message;
    bool isRecved;
};

class Windows {
public:
    int begin;                     // 起始位置的索引
    int end;                       // 结束位置的索引
    uint16_t base;                 // 滑动窗口的base
    WindowsItem buffer[MAX_SIZE];  // 存储数据包的缓冲区

    // 判断发送缓冲区是否为空
    bool isEmpty() { return begin == end; }

    // 判断发送缓冲区是否已满
    bool isFull() { return nextIndex(end) == begin; }

    // 获取下n个索引的位置
    int nextIndex(int index, int n = 1) { return (index + n) % MAX_SIZE; }

    // seq是否在[base, base + N -1]中
    bool isValid(uint16_t seq) { return (uint16_t)(seq - base) < WIN_SIZE; }

    // 得到item在buffer中的索引
    int getIndex(WindowsItem* item) { return item - buffer; }

    // 获取seq对应的数据包指针
    WindowsItem* getItem(uint16_t seq) {
        if (!isValid(seq))
            return nullptr;
        return buffer + nextIndex(begin, (uint16_t)(seq - base));
    }

    // 获取下一个空位置的数据包指针，如果缓冲区已满则返回 nullptr
    WindowsItem* nextEmpty() {
        if (isFull())
            return nullptr;

        WindowsItem* retPtr = buffer + end;
        retPtr->isRecved = false;
        end = nextIndex(end);

        return retPtr;
    }

    // 弹出一个数据包指针，如果还没有接收到该数据包则返回 nullptr
    WindowsItem* pop() {
        if (!buffer[begin].isRecved)
            return nullptr;

        WindowsItem* retPtr = buffer + begin;
        begin = nextIndex(begin);
        base++;

        return retPtr;
    }
};

// 计算校验和的函数，传入伪首部指针作为参数
uint16_t Message::calChecksum(PseudoHeader* pseudoHeader) {
    uint32_t sum = 0;

    // 计算伪首部的校验和
    for (int i = 0; i < sizeof(PseudoHeader) / 2; i++) {
        sum += ((uint16_t*)pseudoHeader)[i];
        sum = (sum & 0xffff) + (sum >> 16);
    }

    // 计算数据的校验和
    for (int i = 0; i < sizeof(Message) / 2; i++) {
        sum += ((uint16_t*)this)[i];
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return (uint16_t)sum;
}

// 打印消息信息的函数
void Message::printMessage(const char* typeStr) {
    printf(
        "%s{ Package [SYN:%d] [ACK:%d] [FIN:%d] [FHD:%d] [Checksum:%d] "
        "[ack:%d] [seq:%d] [Len:%d]}\n",
        typeStr, (bool)getFlag(SYN), (bool)getFlag(ACK), (bool)getFlag(FIN),
        (bool)getFlag(FHD), checksum, ack, seq, getLen());
}

#endif
