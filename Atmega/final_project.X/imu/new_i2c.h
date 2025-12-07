/* * File:   new_i2c.h
 * 避免与旧的 i2c.h/lib_i2c_imu.a 冲突的 I2C 驱动头文件。
 * 所有函数都以 NewI2C_ 为前缀。
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef NEW_I2C_H
#define NEW_I2C_H

// --- 频率定义 ---
#ifndef F_CPU
#define F_CPU 16000000UL 
#endif
#define I2C_SCL_FREQ_HZ 100000UL // 100kHz 适用于大多数 IMU

// --- I2C Operation Status Codes ---
// 虽然我们使用状态检查，但为了简化 API，返回类型仍然是 void 或 uint8_t。
// 状态码主要用于内部检查和调试。
typedef enum {
    NEW_I2C_SUCCESS = 0,
    NEW_I2C_ERROR_START,
    NEW_I2C_ERROR_ADDR_W,
    NEW_I2C_ERROR_ADDR_R,
    NEW_I2C_ERROR_DATA,
    NEW_I2C_ERROR_TIMEOUT,
    NEW_I2C_ERROR_OTHER
} NewI2C_Status;

// --- Core Function Declarations ---

/**
 * @brief 初始化 I2C (TWI) 接口。
 */
void NewI2C_init(uint8_t prescaler_val); // prescaler_val 在新版本中可能被忽略

/**
 * @brief 致命错误处理函数（非阻塞）。
 */
void NewI2C_ERROR(NewI2C_Status status);

/**
 * @brief 发送 I2C START 条件。
 */
NewI2C_Status NewI2C_start();

/**
 * @brief 发送 I2C REPEATED START 条件。
 */
NewI2C_Status NewI2C_repStart();

/**
 * @brief 发送 I2C STOP 条件。
 */
void NewI2C_stop();

/**
 * @brief 启动写操作（发送设备地址 + W）。
 * @param addr: 从机地址 (7位)。
 * @return 状态码。
 */
NewI2C_Status NewI2C_writeBegin(uint8_t addr);

/**
 * @brief 启动读操作（发送设备地址 + R）。
 * @param addr: 从机地址 (7位)。
 * @return 状态码。
 */
NewI2C_Status NewI2C_readBegin(uint8_t addr);

/**
 * @brief 写入单个字节数据。
 * @param data: 要写入的字节。
 * @return 状态码。
 */
NewI2C_Status NewI2C_write(uint8_t data);

/**
 * @brief 从从机读取一个字节数据。
 * @param ack: 是否发送 ACK (true: 继续读取, false: 最后一个字节)。
 * @return 读取到的字节。
 */
uint8_t NewI2C_read(bool ack);


// --- Composite Operation Declarations (与旧库的 API 兼容) ---

/**
 * @brief 写入单个寄存器（START, ADDR+W, REG, DATA, STOP）。
 */
void NewI2C_writeRegister(uint8_t addr, uint8_t data, uint8_t reg);

/**
 * @brief 读取单个寄存器（START, ADDR+W, REG, REP_START, ADDR+R, READ, STOP）。
 */
void NewI2C_readRegister(uint8_t addr, uint8_t* data_addr, uint8_t reg);

/**
 * @brief 完整的读取操作：发送寄存器地址，然后接收数据流。
 * @param data_addr: 接收数据的缓冲区。
 * @param addr: 从机地址 (7位)。
 * @param reg: 要开始读取的寄存器地址。
 * @param len: 要读取的字节数。
 */
void NewI2C_readCompleteStream(uint8_t* data_addr, uint8_t addr, uint8_t reg, int len);


#endif /* NEW_I2C_H */