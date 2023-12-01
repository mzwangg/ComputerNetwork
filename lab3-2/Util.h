// 防止头文件重复包含
#ifndef LAB3_UTIL_H
#define LAB3_UTIL_H

#include <stdint.h>
#include <winsock.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
using namespace std;

// 参数宏定义
#define MSS 4000                 // 最大报文段长度
#define LOSS_RATE 5              // 丢包率
#define RTO 5                    // 等待时间
#define SEED 0                   // 随机数种子
#define WIN_SIZE 12              // 窗口大小
#define MAX_SIZE (WIN_SIZE + 1)  // 循环队列大小
#define SEQ_BITS 16              // 存储seq的位数
#define PRINT_LOG 1              // 是否打印日志

const uint16_t maxSeqNum = (1 << SEQ_BITS) - 1;         // 最大的seq数
#define nextSeq(seq) (((seq) + 1) & maxSeqNum)          // 下一个seq
#define prevSeq(seq) (((seq) + maxSeqNum) & maxSeqNum)  // 上一个seq

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
struct Message {
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

    // 清除指定标志位
    void clearFlag(uint16_t flag) { flagAndLength &= ~flag; }

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
        this->ack = ack & ((1 << SEQ_BITS) - 1);
        this->seq = seq & ((1 << SEQ_BITS) - 1);
        setFlag(flags);
        setLen(dataSize);
        setData(dataPtr);
        setChecksum(sendPseudoHeader);
    }

    // 校验和计算
    uint16_t calChecksum(PseudoHeader* pseudoHeader);

    // 打印消息信息
    void printMessage();
};

// 伪首部构造函数，初始化协议相关字段
PseudoHeader::PseudoHeader() {
    this->zero = 0;
    this->protocol = 13;             // UDP 协议
    this->length = sizeof(Message);  // 伪首部长度为消息结构体的大小
}

class SendBuffer {
public:
    int begin;                 // 起始位置的索引
    int end;                   // 结束位置的索引
    Message buffer[MAX_SIZE];  // 存储数据包的缓冲区

    // 判断发送缓冲区是否为空
    bool isEmpty() { return begin == end; }

    // 判断发送缓冲区是否已满
    bool isFull() { return nextIndex(end) == begin; }

    // 获取下一个索引位置
    int nextIndex(int index) { return (index + 1) % MAX_SIZE; }

    // 获取下一个空位置的数据包指针，如果缓冲区已满则返回 nullptr
    Message* nextEmpty() {
        if (isFull())
            return nullptr;

        Message* retPtr = buffer + end;
        end = nextIndex(end);

        return retPtr;
    }

    // 弹出一个数据包指针，如果缓冲区为空则返回 nullptr
    Message* pop() {
        if (isEmpty())
            return nullptr;

        Message* retPtr = buffer + begin;
        begin = nextIndex(begin);

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
void Message::printMessage() {
    printf(
        "{ Package [SYN:%d] [ACK:%d] [FIN:%d] [FHD:%d] [Checksum:%d] [ack:%d] "
        "[seq:%d] "
        "[Len:%d]}\n",
        (bool)getFlag(SYN), (bool)getFlag(ACK), (bool)getFlag(FIN),
        (bool)getFlag(FHD), checksum, ack, seq, getLen());
}

#endif
