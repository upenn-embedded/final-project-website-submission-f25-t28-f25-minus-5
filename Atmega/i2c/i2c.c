#include <avr/io.h>
#include "i2c.h"

#ifndef F_CPU
#define F_CPU 16000000UL   // adjust if your clock is different
#endif

#define F_SCL     400000UL
#define PRESCALER 1
#define TWBR_VALUE ((((F_CPU / F_SCL) / PRESCALER) - 16) / 2)

// ATmega328PB has TWI0/TWI1 ? map legacy names to TWI0
#if defined(__AVR_ATmega328PB__) || defined(__ATmega328PB__)
#define TWBR  TWBR0
#define TWSR  TWSR0
#define TWAR  TWAR0
#define TWDR  TWDR0
#define TWCR  TWCR0
#define TWAMR TWAMR0
#endif

void i2c_init(void)
{
    // prescaler = 1
    TWSR = 0x00;
    TWBR = (uint8_t)TWBR_VALUE;

    // Enable TWI
    TWCR = (1 << TWEN);
}

void i2c_start(uint8_t addr)
{
    // Send START
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));

    // Send address
    TWDR = addr;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

void i2c_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

uint8_t i2c_read_ack(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    while (!(TWCR & (1 << TWINT)));
    return TWDR;
}

uint8_t i2c_read_nack(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    return TWDR;
}

void i2c_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
    // No need to wait for stop to complete
}
