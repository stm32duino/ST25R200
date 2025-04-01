/******************************************************************************
  * \attention
  *
  * <h2><center>&copy; COPYRIGHT 2021 STMicroelectronics</center></h2>
  *
  * Licensed under ST MIX MYLIBERTY SOFTWARE LICENSE AGREEMENT (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        www.st.com/mix_myliberty
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied,
  * AND SPECIFICALLY DISCLAIMING THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
******************************************************************************/

/*! \file
 *
 *  \author SRA
 *
 *  \brief Implementation of ST25R200 communication.
 *
 */

/*
******************************************************************************
* INCLUDES
******************************************************************************
*/
#include "rfal_rfst25r200.h"
#include "st25r200_com.h"
#include "st25r200.h"
#include "nfc_utils.h"


/*
******************************************************************************
* LOCAL DEFINES
******************************************************************************
*/

#define ST25R200_OPTIMIZE              true                           /*!< Optimization switch: false always write value to register     */
#define ST25R200_I2C_ADDR              (0xA0U >> 1)                   /*!< ST25R200's default I2C address                                */
#define ST25R200_REG_LEN               1U                             /*!< Byte length of a ST25R200 register                            */

#define ST25R200_FIFO_LOAD             (0x80U)                        /*!< ST25R200 Operation Mode: FIFO Load                            */
#define ST25R200_FIFO_READ             (0x9FU)                        /*!< ST25R200 Operation Mode: FIFO Read                            */


#define ST25R200_BUF_LEN               (ST25R200_CMD_LEN+ST25R200_FIFO_DEPTH) /*!< ST25R200 communication buffer: CMD + FIFO length      */

/*
******************************************************************************
* LOCAL VARIABLES
******************************************************************************
*/

/*
******************************************************************************
* LOCAL FUNCTION PROTOTYPES
******************************************************************************
*/


/*
******************************************************************************
* GLOBAL FUNCTIONS
******************************************************************************
*/
/*******************************************************************************/
/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ReadRegister(uint8_t reg, uint8_t *val)
{
  return st25r200ReadMultipleRegisters(reg, val, ST25R200_REG_LEN);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ReadMultipleRegisters(uint8_t reg, uint8_t *values, uint16_t length)
{
  if (length > 0U) {

    /* Setting Transaction Parameters */
    dev_spi->beginTransaction(SPISettings(spi_speed, MSBFIRST, SPI_MODE1));
    digitalWrite(cs_pin, LOW);

    uint8_t response = dev_spi->transfer((reg | ST25R200_READ_MODE));
    dev_spi->transfer((void *)values, length);

    digitalWrite(cs_pin, HIGH);
    dev_spi->endTransaction();

    if (isr_pending) {
      st25r200Isr();
      isr_pending = false;
    }
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200WriteRegister(uint8_t reg, uint8_t val)
{
  uint8_t value = val;               /* MISRA 17.8: use intermediate variable */
  return st25r200WriteMultipleRegisters(reg, &value, ST25R200_REG_LEN);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200WriteMultipleRegisters(uint8_t reg, const uint8_t *values, uint16_t length)
{
  if (length > 0U) {

    uint8_t tx[256];

    if (values != NULL) {
      (void)memcpy(tx, values, length);
    }
    /* Setting Transaction Parameters */
    dev_spi->beginTransaction(SPISettings(spi_speed, MSBFIRST, SPI_MODE1));
    digitalWrite(cs_pin, LOW);

    uint8_t response = dev_spi->transfer((reg | ST25R200_WRITE_MODE));
    dev_spi->transfer((void *) tx, length);

    digitalWrite(cs_pin, HIGH);
    dev_spi->endTransaction();

    if (isr_pending) {
      st25r200Isr();
      isr_pending = false;
    }
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200WriteFifo(const uint8_t *values, uint16_t length)
{
  if (length > ST25R200_FIFO_DEPTH) {
    return ERR_PARAM;
  }

  st25r200WriteMultipleRegisters(ST25R200_FIFO_ACCESS, values, length);

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ReadFifo(uint8_t *buf, uint16_t length)
{
  if (length > 0U) {
    if (length > ST25R200_FIFO_DEPTH) {
      return ERR_PARAM;
    }

    st25r200ReadMultipleRegisters(ST25R200_FIFO_ACCESS, buf, length);
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ExecuteCommand(uint8_t cmd)
{

  /* Setting Transaction Parameters */
  dev_spi->beginTransaction(SPISettings(spi_speed, MSBFIRST, SPI_MODE1));
  digitalWrite(cs_pin, LOW);

  dev_spi->transfer((cmd | ST25R200_CMD_MODE));

  digitalWrite(cs_pin, HIGH);
  dev_spi->endTransaction();

  if (isr_pending) {
    st25r200Isr();
    isr_pending = false;
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ReadTestRegister(uint8_t reg, uint8_t *val)
{

  /* Setting Transaction Parameters */
  dev_spi->beginTransaction(SPISettings(spi_speed, MSBFIRST, SPI_MODE1));
  digitalWrite(cs_pin, LOW);

  dev_spi->transfer(ST25R200_CMD_TEST_ACCESS);
  dev_spi->transfer((reg | ST25R200_READ_MODE));

  dev_spi->transfer((void *)val, ST25R200_REG_LEN);

  digitalWrite(cs_pin, HIGH);
  dev_spi->endTransaction();

  if (isr_pending) {
    st25r200Isr();
    isr_pending = false;
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200WriteTestRegister(uint8_t reg, uint8_t val)
{

  uint8_t value = val;               /* MISRA 17.8: use intermediate variable */
  /* Setting Transaction Parameters */
  dev_spi->beginTransaction(SPISettings(spi_speed, MSBFIRST, SPI_MODE1));
  digitalWrite(cs_pin, LOW);

  dev_spi->transfer(ST25R200_CMD_TEST_ACCESS);

  dev_spi->transfer((reg | ST25R200_WRITE_MODE));

  dev_spi->transfer((void *)&value, ST25R200_REG_LEN);

  digitalWrite(cs_pin, HIGH);
  dev_spi->endTransaction();


  if (isr_pending) {
    st25r200Isr();
    isr_pending = false;
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200WriteMultipleTestRegister(uint8_t reg, const uint8_t *values, uint8_t length)
{
  if (length > 0U) {

    uint8_t tx[256];

    if (values != NULL) {
      (void)memcpy(tx, values, length);
    }

    /* Setting Transaction Parameters */
    dev_spi->beginTransaction(SPISettings(spi_speed, MSBFIRST, SPI_MODE1));
    digitalWrite(cs_pin, LOW);

    dev_spi->transfer(ST25R200_CMD_TEST_ACCESS);

    dev_spi->transfer((reg | ST25R200_WRITE_MODE));

    dev_spi->transfer((void *)values, length);

    digitalWrite(cs_pin, HIGH);
    dev_spi->endTransaction();

    if (isr_pending) {
      st25r200Isr();
      isr_pending = false;
    }
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ClrRegisterBits(uint8_t reg, uint8_t clr_mask)
{
  ReturnCode ret;
  uint8_t    rdVal;

  /* Read current reg value */
  EXIT_ON_ERR(ret, st25r200ReadRegister(reg, &rdVal));

  /* Only perform a Write if value to be written is different */
  if (ST25R200_OPTIMIZE && (rdVal == (uint8_t)(rdVal & ~clr_mask))) {
    return ERR_NONE;
  }

  /* Write new reg value */
  return st25r200WriteRegister(reg, (uint8_t)(rdVal & ~clr_mask));
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200SetRegisterBits(uint8_t reg, uint8_t set_mask)
{
  ReturnCode ret;
  uint8_t    rdVal;

  /* Read current reg value */
  EXIT_ON_ERR(ret, st25r200ReadRegister(reg, &rdVal));

  /* Only perform a Write if the value to be written is different */
  if (ST25R200_OPTIMIZE && (rdVal == (rdVal | set_mask))) {
    return ERR_NONE;
  }

  /* Write new reg value */
  return st25r200WriteRegister(reg, (rdVal | set_mask));
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ChangeRegisterBits(uint8_t reg, uint8_t valueMask, uint8_t value)
{
  return st25r200ModifyRegister(reg, valueMask, (valueMask & value));
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ModifyRegister(uint8_t reg, uint8_t clr_mask, uint8_t set_mask)
{
  ReturnCode ret;
  uint8_t    rdVal;
  uint8_t    wrVal;

  /* Read current reg value */
  EXIT_ON_ERR(ret, st25r200ReadRegister(reg, &rdVal));

  /* Compute new value */
  wrVal  = (uint8_t)(rdVal & ~clr_mask);
  wrVal |= set_mask;

  /* Only perform a Write if the value to be written is different */
  if (ST25R200_OPTIMIZE && (rdVal == wrVal)) {
    return ERR_NONE;
  }

  /* Write new reg value */
  return st25r200WriteRegister(reg, wrVal);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::st25r200ChangeTestRegisterBits(uint8_t reg, uint8_t valueMask, uint8_t value)
{
  ReturnCode ret;
  uint8_t    rdVal;
  uint8_t    wrVal;

  /* Read current reg value */
  EXIT_ON_ERR(ret, st25r200ReadTestRegister(reg, &rdVal));

  /* Compute new value */
  wrVal  = (uint8_t)(rdVal & ~valueMask);
  wrVal |= (uint8_t)(value & valueMask);

  /* Only perform a Write if the value to be written is different */
  if (ST25R200_OPTIMIZE && (rdVal == wrVal)) {
    return ERR_NONE;
  }

  /* Write new reg value */
  return st25r200WriteTestRegister(reg, wrVal);
}


/*******************************************************************************/
bool RfalRfST25R200Class::st25r200CheckReg(uint8_t reg, uint8_t mask, uint8_t val)
{
  uint8_t regVal;

  regVal = 0;
  st25r200ReadRegister(reg, &regVal);

  return ((regVal & mask) == val);
}


/*******************************************************************************/
bool RfalRfST25R200Class::st25r200IsRegValid(uint8_t reg)
{
  if (!(((int16_t)reg >= (int16_t)ST25R200_REG_OPERATION) && (reg < ST25R200_FIFO_ACCESS))) {
    return false;
  }
  return true;
}
