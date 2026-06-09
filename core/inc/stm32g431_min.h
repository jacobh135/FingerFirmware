#ifndef STM32G431_MIN_H
#define STM32G431_MIN_H

#include <stdint.h>

#define PERIPH_BASE        (0x40000000UL)
#define APB1PERIPH_BASE    PERIPH_BASE
#define AHB2PERIPH_BASE    (PERIPH_BASE + 0x08000000UL)
#define RCC_BASE           (0x40021000UL)
#define GPIOA_BASE         (AHB2PERIPH_BASE + 0x0000UL)
#define GPIOB_BASE         (AHB2PERIPH_BASE + 0x0400UL)
#define I2C1_BASE          (APB1PERIPH_BASE + 0x5400UL)

typedef struct {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFRL;
    volatile uint32_t AFRH;
    volatile uint32_t BRR;
} GPIO_TypeDef;

typedef struct {
    volatile uint32_t CR;
    volatile uint32_t ICSCR;
    volatile uint32_t CFGR;
    volatile uint32_t PLLCFGR;
    volatile uint32_t RESERVED0;
    volatile uint32_t CRRCR;
    volatile uint32_t CIER;
    volatile uint32_t CIFR;
    volatile uint32_t CICR;
    volatile uint32_t RESERVED1;
    volatile uint32_t AHB1RSTR;
    volatile uint32_t AHB2RSTR;
    volatile uint32_t AHB3RSTR;
    volatile uint32_t RESERVED2;
    volatile uint32_t APB1RSTR1;
    volatile uint32_t APB1RSTR2;
    volatile uint32_t APB2RSTR;
    volatile uint32_t RESERVED3;
    volatile uint32_t AHB1ENR;
    volatile uint32_t AHB2ENR;
    volatile uint32_t AHB3ENR;
    volatile uint32_t RESERVED4;
    volatile uint32_t APB1ENR1;
    volatile uint32_t APB1ENR2;
    volatile uint32_t APB2ENR;
} RCC_TypeDef;

typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;
    volatile uint32_t OAR2;
    volatile uint32_t TIMINGR;
    volatile uint32_t TIMEOUTR;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t PECR;
    volatile uint32_t RXDR;
    volatile uint32_t TXDR;
} I2C_TypeDef;

#define RCC                ((RCC_TypeDef *)RCC_BASE)
#define GPIOA              ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB              ((GPIO_TypeDef *)GPIOB_BASE)
#define I2C1               ((I2C_TypeDef *)I2C1_BASE)

#define RCC_AHB2ENR_GPIOAEN (1UL << 0)
#define RCC_AHB2ENR_GPIOBEN (1UL << 1)
#define RCC_APB1ENR1_I2C1EN (1UL << 21)

#define RCC_APB1RSTR1_I2C1RST (1UL << 21)

#define I2C_CR1_PE         (1UL << 0)

#define I2C_CR2_RD_WRN     (1UL << 10)
#define I2C_CR2_START      (1UL << 13)
#define I2C_CR2_STOP       (1UL << 14)
#define I2C_CR2_NBYTES_Pos 16U
#define I2C_CR2_AUTOEND    (1UL << 25)

#define I2C_ISR_TXE        (1UL << 0)
#define I2C_ISR_TXIS       (1UL << 1)
#define I2C_ISR_RXNE       (1UL << 2)
#define I2C_ISR_NACKF      (1UL << 4)
#define I2C_ISR_STOPF      (1UL << 5)
#define I2C_ISR_TC         (1UL << 6)
#define I2C_ISR_BERR       (1UL << 8)
#define I2C_ISR_ARLO       (1UL << 9)
#define I2C_ISR_OVR        (1UL << 10)
#define I2C_ISR_TIMEOUT    (1UL << 12)
#define I2C_ISR_BUSY       (1UL << 15)

#define I2C_ICR_NACKCF     (1UL << 4)
#define I2C_ICR_STOPCF     (1UL << 5)
#define I2C_ICR_BERRCF     (1UL << 8)
#define I2C_ICR_ARLOCF     (1UL << 9)
#define I2C_ICR_OVRCF      (1UL << 10)
#define I2C_ICR_TIMOUTCF   (1UL << 12)

#endif
