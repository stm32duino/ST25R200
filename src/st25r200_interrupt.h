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
 *  \brief ST25R200 Interrupt header file
 *
 *
 * \addtogroup RFAL
 * @{
 *
 * \addtogroup RFAL-HAL
 * \brief RFAL Hardware Abstraction Layer
 * @{
 *
 * \addtogroup ST25R200
 * \brief RFAL ST25R200 Driver
 * @{
 *
 * \addtogroup ST25R200_Interrupt
 * \brief RFAL ST25R200 Interrupt
 * @{
 *
 */

#ifndef ST25R200_INTERRUPT_H
#define ST25R200_INTERRUPT_H

/*
******************************************************************************
* INCLUDES
******************************************************************************
*/


/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/

#define ST25R200_IRQ_MASK_ALL             (uint32_t)(0xFFFFFFFFUL)  /*!< All ST25R200 interrupt sources                             */
#define ST25R200_IRQ_MASK_NONE            (uint32_t)(0x00000000UL)  /*!< No ST25R200 interrupt source                               */

/* Main interrupt register */
#define ST25R200_IRQ_MASK_SUBC_START      (uint32_t)(0x00000080U)   /*!< ST25R200 subcarrier start interrupt                        */
#define ST25R200_IRQ_MASK_COL             (uint32_t)(0x00000040U)   /*!< ST25R200 bit collision interrupt                           */
#define ST25R200_IRQ_MASK_WL              (uint32_t)(0x00000020U)   /*!< ST25R200 FIFO water level interrupt                        */
#define ST25R200_IRQ_MASK_RX_REST         (uint32_t)(0x00000010U)   /*!< ST25R200 automatic reception restart interrupt             */
#define ST25R200_IRQ_MASK_RXE             (uint32_t)(0x00000008U)   /*!< ST25R200 end of receive interrupt                          */
#define ST25R200_IRQ_MASK_RXS             (uint32_t)(0x00000004U)   /*!< ST25R200 start of receive interrupt                        */
#define ST25R200_IRQ_MASK_TXE             (uint32_t)(0x00000002U)   /*!< ST25R200 end of transmission interrupt                     */
#define ST25R200_IRQ_MASK_RFU1            (uint32_t)(0x00000001U)   /*!< ST25R200 RFU interrupt                                     */

/* Timer and Error interrupt register */
#define ST25R200_IRQ_MASK_GPE             (uint32_t)(0x00008000U)   /*!< ST25R200 general purpose timer expired interrupt           */
#define ST25R200_IRQ_MASK_NRE             (uint32_t)(0x00004000U)   /*!< ST25R200 no-response timer expired interrupt               */
#define ST25R200_IRQ_MASK_RFU2            (uint32_t)(0x00002000U)   /*!< ST25R200 RFU interrupt                                     */
#define ST25R200_IRQ_MASK_RFU3            (uint32_t)(0x00001000U)   /*!< ST25R200 RFU interrupt                                     */
#define ST25R200_IRQ_MASK_CRC             (uint32_t)(0x00000800U)   /*!< ST25R200 CRC error interrupt                               */
#define ST25R200_IRQ_MASK_PAR             (uint32_t)(0x00000400U)   /*!< ST25R200 parity error interrupt                            */
#define ST25R200_IRQ_MASK_HFE             (uint32_t)(0x00000200U)   /*!< ST25R200 hard framing error interrupt                      */
#define ST25R200_IRQ_MASK_SFE             (uint32_t)(0x00000100U)   /*!< ST25R200 soft framing error interrupt                      */

/* Wake-up interrupt register */
#define ST25R200_IRQ_MASK_RFU4            (uint32_t)(0x00800000U)   /*!< ST25R200 RFU interrupt                                     */
#define ST25R200_IRQ_MASK_RFU5            (uint32_t)(0x00400000U)   /*!< ST25R200 RFU interrupt                                     */
#define ST25R200_IRQ_MASK_RFU6            (uint32_t)(0x00200000U)   /*!< ST25R200 RFU interrupt                                     */
#define ST25R200_IRQ_MASK_DCT             (uint32_t)(0x00100000U)   /*!< ST25R200 termination of direct command interrupt           */
#define ST25R200_IRQ_MASK_WUQ             (uint32_t)(0x00080000U)   /*!< ST25R200 wake-up Q-Channel interrupt                       */
#define ST25R200_IRQ_MASK_WUI             (uint32_t)(0x00040000U)   /*!< ST25R200 wake-up I-Channel interrupt                       */
#define ST25R200_IRQ_MASK_WUT             (uint32_t)(0x00020000U)   /*!< ST25R200 wake-up interrupt                                 */
#define ST25R200_IRQ_MASK_OSC             (uint32_t)(0x00010000U)   /*!< ST25R200 oscillator stable interrupt                       */


/*! Holds current and previous interrupt callback pointer as well as current Interrupt status and mask */
typedef struct {
  void (*prevCallback)(void);      /*!< call back function for ST25R200 interrupt          */
  void (*callback)(void);          /*!< call back function for ST25R200 interrupt          */
  uint32_t  status;                /*!< latest interrupt status                             */
  uint32_t  mask;                  /*!< Interrupt mask. Negative mask = ST25R200 mask regs */
} st25r200Interrupt;

/*
******************************************************************************
* GLOBAL FUNCTION PROTOTYPES
******************************************************************************
*/


#endif /* ST25R200_ISR_H */

/**
  * @}
  *
  * @}
  *
  * @}
  *
  * @}
  */
