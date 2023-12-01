// 防止头文件重复包含
#ifndef LAB3_UTIL_H
#define LAB3_UTIL_H

#include <winsock.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
using namespace std;

// 参数宏定义
#define MSS 4000
#define LOSS_RATE 2
#define RTO 2
#define SEED 1

// 是否打印日志
const bool PRINT_LOG = true;
// const bool PRINT_LOG = false;

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
    uint16_t ack;                // ack， 目前未使用
    uint16_t seq;                // seq, 目前仅使用最低位
    uint16_t flagAndLength = 0;  // ACK(15)|SYN(14)|FIN(13)|FHD(12)|LEN(11-0)
    uint16_t checksum;           // 校验和
    uint8_t data[MSS];           // 数据段

    // 成员函数
    // 获取指定标志位
    uint16_t getFlag(uint16_t flag) { return flagAndLength & flag; }

    // 设置指定标志位
    void setFlag(uint16_t flag) { flagAndLength |= flag; }

    // 清除指定标志位
    void clearFlag(uint16_t flag) { flagAndLength &= ~flag; }

    // 获取报文段长度
    uint16_t getLen() { return flagAndLength & 0x0FFF; }

    // 设置报文段长度
    void setLen(uint16_t len) { flagAndLength |= (len & 0x0FFF); }

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
        "{ Package [SYN:%d] [ACK:%d] [FIN:%d] [Checksum:%d] [ack:%d] [seq:%d] "
        "[Len:%d]}\n",
        (bool)getFlag(SYN), (bool)getFlag(ACK), (bool)getFlag(FIN), checksum,
        ack, seq, getLen());
}

#endif
