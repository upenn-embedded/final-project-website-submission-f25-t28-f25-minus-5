/* * File:   new_i2c.c
 * ????? i2c.c/lib_i2c_imu.a ??? I2C ???????
 * ?????? NewI2C_ ????
 */

#include "new_i2c.h"
#include <avr/io.h>     // ?? TWI ?????: TWCR, TWDR, TWSR, TWBR
#include <util/twi.h>   // ?? TWI ???: TW_START, TW_MT_SLA_ACK, etc.
#include <util/delay.h>
#include <stdio.h>      // For ERROR() printing

// --------------------------------------------------------------------------
// ?? ATmega328PB ??????
// (TWCR, TWBR, TWDR ?? 328PB ??????? TWCR0, TWBR0, TWDR0)
// ?????????**??**
// --------------------------------------------------------------------------
#if defined(__AVR_ATmega328PB__) || defined(__ATmega328PB__)
#define TWBR    TWBR0
#define TWSR    TWSR0
#define TWAR    TWAR0
#define TWDR    TWDR0
#define TWCR    TWCR0
#define TWAMR   TWAMR0
#endif

// --- TWI Timeout constant ---
#define TWI_TIMEOUT 20000 

// --- Internal Helper Functions Declarations ---
static NewI2C_Status twi_wait_for_completion(uint8_t expected_status_mask, uint8_t expected_status_code);
static NewI2C_Status twi_transmit(uint8_t data, uint8_t expected_status_code);
static NewI2C_Status twi_wait_for_start_or_repstart(uint8_t expected_status_code);
static NewI2C_Status twi_wait_for_addr_ack(uint8_t expected_status_code);
static NewI2C_Status twi_wait_for_data_ack(uint8_t expected_status_code);

// --- Error Handling (Non-blocking) ---

void NewI2C_ERROR(NewI2C_Status status)
{
    // ????????
    printf("--- New I2C ERROR! Status Code: %d, TWI_Status: 0x%02X ---\r\n", status, TWSR & TW_STATUS_MASK);
    return;
}

// --- Internal Helper Functions Implementations ---

/**
 * @brief ?? TWI ??????????????
 * @param expected_status_mask: ?????? (??? TW_STATUS_MASK)?
 * @param expected_status_code: ?????? (?? TW_START)?
 * @return NEW_I2C_SUCCESS ?????????
 */
static NewI2C_Status twi_wait_for_completion(uint8_t expected_status_mask, uint8_t expected_status_code)
{
    uint32_t timeout = TWI_TIMEOUT;
    // ?? TWINT ?????? (????)
    while (!(TWCR & (1 << TWINT))) {
        if (--timeout == 0) {
            NewI2C_ERROR(NEW_I2C_ERROR_TIMEOUT);
            return NEW_I2C_ERROR_TIMEOUT;
        }
    }

    // ?? TWI ??
    if ((TWSR & expected_status_mask) != expected_status_code) {
        // ????????????????????? ERROR ??
        return NEW_I2C_ERROR_OTHER; 
    }
    return NEW_I2C_SUCCESS;
}

/**
 * @brief ??????????
 * @param data: ???????
 * @param expected_status_code: ?????? (?? TW_MT_SLA_ACK)?
 * @return ????
 */
static NewI2C_Status twi_transmit(uint8_t data, uint8_t expected_status_code)
{
    TWDR = data;
    // ?? TWINT ?????
    TWCR = (1 << TWINT) | (1 << TWEN);
    
    // ????????? (?? TW_MT_DATA_ACK)
    return twi_wait_for_completion(TW_STATUS_MASK, expected_status_code);
}

// --- I2C Driver Core Functions Implementations ---

void NewI2C_init(uint8_t prescaler_val)
{
    // SCL Frequency = F_CPU / (16 + 2 * TWBR * 4^TWPS)
    // ?? 100kHz SCL at 16MHz F_CPU?TWBR ?? 72
    TWBR = (uint8_t)((F_CPU / I2C_SCL_FREQ_HZ) - 16) / 2;
    
    // ?? Prescaler ? 1 (TWPS1=0, TWPS0=0)
    // ?? prescaler_val ??
    TWSR &= ~((1 << TWPS0) | (1 << TWPS1)); 
    
    // ?? TWI
    TWCR = (1 << TWEN);
}

NewI2C_Status NewI2C_start()
{
    // ?? START ??: ?? TWINT, ?? TWSTA (Start), ?? TWEN (Enable)
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    NewI2C_Status status = twi_wait_for_completion(TW_STATUS_MASK, TW_START);
    if (status != NEW_I2C_SUCCESS) NewI2C_ERROR(NEW_I2C_ERROR_START);
    return status;
}

NewI2C_Status NewI2C_repStart()
{
    // ?? REPEATED START ??: ?? TWINT, ?? TWSTA (Start), ?? TWEN (Enable)
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    NewI2C_Status status = twi_wait_for_completion(TW_STATUS_MASK, TW_REP_START);
    if (status != NEW_I2C_SUCCESS) NewI2C_ERROR(NEW_I2C_ERROR_START);
    return status;
}

void NewI2C_stop()
{
    // ?? STOP ??: ?? TWINT, ?? TWSTO (Stop), ?? TWEN (Enable)
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    // ???? STOP ??
}

NewI2C_Status NewI2C_writeBegin(uint8_t addr)
{
    // ?????? + ?? (SLA+W)
    NewI2C_Status status = twi_transmit((addr << 1) | 0x00, TW_MT_SLA_ACK);
    if (status != NEW_I2C_SUCCESS) NewI2C_ERROR(NEW_I2C_ERROR_ADDR_W);
    return status;
}

NewI2C_Status NewI2C_readBegin(uint8_t addr)
{
    // ?????? + ?? (SLA+R)
    NewI2C_Status status = twi_transmit((addr << 1) | 0x01, TW_MR_SLA_ACK);
    if (status != NEW_I2C_SUCCESS) NewI2C_ERROR(NEW_I2C_ERROR_ADDR_R);
    return status;
}

NewI2C_Status NewI2C_write(uint8_t data)
{
    // ????
    NewI2C_Status status = twi_transmit(data, TW_MT_DATA_ACK);
    if (status != NEW_I2C_SUCCESS) NewI2C_ERROR(NEW_I2C_ERROR_DATA);
    return status;
}

uint8_t NewI2C_read(bool ack)
{
    uint8_t expected_status = ack ? TW_MR_DATA_ACK : TW_MR_DATA_NACK;
    
    // ????: ?? TWINT, ?? TWEN.
    // TWEA (ACK ?) ?????? ACK (????) ? NACK (??????)?
    if (ack) {
        // ?? ACK (TWEA ???)
        TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    } else {
        // ?? NACK (TWEA ???) - ??????
        TWCR = (1 << TWINT) | (1 << TWEN);
    }

    // ??????
    if (twi_wait_for_completion(TW_STATUS_MASK, expected_status) != NEW_I2C_SUCCESS) {
        // ???????????? ERROR
        NewI2C_ERROR(NEW_I2C_ERROR_DATA);
        return 0xFF; // ?????
    }
    
    // ????
    return TWDR;
}

// --- Composite Operations (???? API ??) ---

void NewI2C_writeRegister(uint8_t addr, uint8_t data, uint8_t reg)
{
    if (NewI2C_start() != NEW_I2C_SUCCESS) return;
    if (NewI2C_writeBegin(addr) != NEW_I2C_SUCCESS) goto stop;
    if (NewI2C_write(reg) != NEW_I2C_SUCCESS) goto stop;
    if (NewI2C_write(data) != NEW_I2C_SUCCESS) goto stop;
stop:
    NewI2C_stop();
}

void NewI2C_readRegister(uint8_t addr, uint8_t* data_addr, uint8_t reg)
{
    // 1. ???????
    if (NewI2C_start() != NEW_I2C_SUCCESS) return;
    if (NewI2C_writeBegin(addr) != NEW_I2C_SUCCESS) goto stop;
    if (NewI2C_write(reg) != NEW_I2C_SUCCESS) goto stop;

    // 2. ?????????
    if (NewI2C_repStart() != NEW_I2C_SUCCESS) return;
    if (NewI2C_readBegin(addr) != NEW_I2C_SUCCESS) goto stop;
    *data_addr = NewI2C_read(false); // ????????? NACK

stop:
    NewI2C_stop();
}


void NewI2C_readCompleteStream(uint8_t* data_addr, uint8_t addr, uint8_t reg, int len)
{
    if (len <= 0) return;

    // 1. ??????? (???)
    if (NewI2C_start() != NEW_I2C_SUCCESS) return;
    if (NewI2C_writeBegin(addr) != NEW_I2C_SUCCESS) goto stop;
    // ?????????????
    if (NewI2C_write(reg) != NEW_I2C_SUCCESS) goto stop; 

    // 2. ?? Repeated START ??
    if (NewI2C_repStart() != NEW_I2C_SUCCESS) return;
    
    // 3. ????? (???)
    if (NewI2C_readBegin(addr) != NEW_I2C_SUCCESS) goto stop;

    // 4. ?????
    for (int i = 0; i < len; i++) {
        // ????????????????? ACK (i < len - 1)
        data_addr[i] = NewI2C_read(i < len - 1); 
    }

stop:
    NewI2C_stop();
}