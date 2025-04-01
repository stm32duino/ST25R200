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
 *  \brief ST25R200 declaration file
 *
 * API:
 * - Initialize ST25R200 driver: #ST25R200Initialize
 * - Deinitialize ST25R200 driver: #ST25R200Deinitialize
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
 * \addtogroup ST25R200_Driver
 * \brief RFAL ST25R200 Driver
 * @{
 *
 */

#ifndef ST25R200_H
#define ST25R200_H
/*
******************************************************************************
* INCLUDES
******************************************************************************
*/
#include "st_errno.h"
#include "st25r200_com.h"

/*
******************************************************************************
* ENABLE SWITCH
******************************************************************************
*/
#ifndef ST25R200
  #error "RFAL: Missing ST25R device selection. Please globally define ST25R200."
#endif /* ST25R200 */

/*
******************************************************************************
* GLOBAL DATATYPES
******************************************************************************
*/


/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/
/* ST25R200 direct commands */
#define ST25R200_CMD_SET_DEFAULT              0x60U    /*!< Puts the chip in default state (same as after power-up) */
#define ST25R200_CMD_STOP                     0x62U    /*!< Stops all activities and clears FIFO                    */
#define ST25R200_CMD_CLEAR_FIFO               0x64U    /*!< Clears FIFO, Collision and IRQ status                   */
#define ST25R200_CMD_CLEAR_RXGAIN             0x66U    /*!< Clears FIFO, Collision and IRQ status                   */
#define ST25R200_CMD_ADJUST_REGULATORS        0x68U    /*!< Adjust regulators                                       */
#define ST25R200_CMD_TRANSMIT                 0x6AU    /*!< Transmit                                                */
#define ST25R200_CMD_TRANSMIT_EOF             0x6CU    /*!< Transmit ISO15693 EOF                                   */
#define ST25R200_CMD_MASK_RECEIVE_DATA        0x70U    /*!< Mask receive data                                       */
#define ST25R200_CMD_UNMASK_RECEIVE_DATA      0x72U    /*!< Unmask receive data                                     */
#define ST25R200_CMD_CALIBRATE_WU             0x74U    /*!< Calibrate Wake-up Measurement                           */
#define ST25R200_CMD_CLEAR_WU_CALIB           0x76U    /*!< Clear Wake-up Calibratation                             */
#define ST25R200_CMD_MEASURE_WU               0x78U    /*!< Measure Wake-up I and Q components                      */
#define ST25R200_CMD_MEASURE_IQ               0x7AU    /*!< Measure I and Q components                              */
#define ST25R200_CMD_SENSE_RF                 0x7CU    /*!< Sense RF on RFI pins                                    */
#define ST25R200_CMD_TRANSPARENT_MODE         0xE0U    /*!< Transparent mode                                        */
#define ST25R200_CMD_START_GP_TIMER           0xE2U    /*!< Start the general purpose timer                         */
#define ST25R200_CMD_START_WUP_TIMER          0xE4U    /*!< Start the wake-up timer                                 */
#define ST25R200_CMD_START_MASK_RECEIVE_TIMER 0xE6U    /*!< Start the mask-receive timer                            */
#define ST25R200_CMD_START_NO_RESPONSE_TIMER  0xE8U    /*!< Start the no-response timer                             */
#define ST25R200_CMD_STOP_NRT                 0xEAU    /*!< Stop No Response Timer                                  */
#define ST25R200_CMD_TEST_ACCESS              0xFCU    /*!< Enable R/W access to the test registers                 */


#define ST25R200_BR_DO_NOT_SET                0xFFU    /*!< Indicates not to change this Bit Rate                   */
#define ST25R200_BR_106_26                    0x00U    /*!< ST25R200 Bit Rate  106 kbps (fc/128) / 26 kbps(fc/512)  */
#define ST25R200_BR_212                       0x01U    /*!< ST25R200 Bit Rate  212 kbps (fc/64)                     */
#define ST25R200_BR_424_53                    0x02U    /*!< ST25R200 Bit Rate  424 kbps (fc/32) / 53 kbps(fc/256)   */
#define ST25R200_BR_848                       0x03U    /*!< ST25R200 Bit Rate  848 kbps (fc/16)                     */

#define ST25R200_REG_DROP_200                 0U       /*!< ST25R200 target drop for regulator adjustment: 200mV    */
#define ST25R200_REG_DROP_250                 1U       /*!< ST25R200 target drop for regulator adjustment: 250mV    */
#define ST25R200_REG_DROP_300                 2U       /*!< ST25R200 target drop for regulator adjustment: 300mV    */
#define ST25R200_REG_DROP_350                 3U       /*!< ST25R200 target drop for regulator adjustment: 350mV    */
#define ST25R200_REG_DROP_400                 4U       /*!< ST25R200 target drop for regulator adjustment: 400mV    */
#define ST25R200_REG_DROP_450                 5U       /*!< ST25R200 target drop for regulator adjustment: 450mV    */
#define ST25R200_REG_DROP_500                 6U       /*!< ST25R200 target drop for regulator adjustment: 500mV    */
#define ST25R200_REG_DROP_550                 7U       /*!< ST25R200 target drop for regulator adjustment: 550mV    */
#define ST25R200_REG_DROP_DO_NOT_SET          0xFFU    /*!< Indicates not to change this setting (regd)             */

#define ST25R200_REG_LEN                      1U       /*!< Number of bytes in a ST25R200 register                  */
#define ST25R200_CMD_LEN                      1U       /*!< ST25R200 CMD length                                     */
#define ST25R200_FIFO_DEPTH                   256U     /*!< Depth of FIFO                                           */

#define ST25R200_WRITE_MODE                   (0U << 7)           /*!< ST25R200 Operation Mode: Write               */
#define ST25R200_READ_MODE                    (1U << 7)           /*!< ST25R200 Operation Mode: Read                */
#define ST25R200_CMD_MODE                     ST25R200_WRITE_MODE /*!< ST25R200 Operation Mode: Direct Command      */
#define ST25R200_FIFO_ACCESS                  (0x5FU)             /*!< ST25R200 FIFO Access                         */


/*
******************************************************************************
* GLOBAL MACROS
******************************************************************************
*/

/*! Enables the Transmitter (Field On) and Receiver */
#define st25r200TxRxOn()             st25r200SetRegisterBits( ST25R200_REG_OPERATION, (ST25R200_REG_OPERATION_rx_en | ST25R200_REG_OPERATION_tx_en ) )

/*! Disables the Transmitter (Field Off) and Receiver                                         */
#define st25r200TxRxOff()            st25r200ClrRegisterBits( ST25R200_REG_OPERATION, (ST25R200_REG_OPERATION_rx_en | ST25R200_REG_OPERATION_tx_en ) )

/*! Disables the Transmitter (Field Off) */
#define st25r200TxOff()              st25r200ClrRegisterBits( ST25R200_REG_OPERATION, ST25R200_REG_OPERATION_tx_en )

/*! Checks if General Purpose Timer is still running by reading gpt_on flag */
#define st25r200IsGPTRunning( )      st25r200CheckReg( ST25R200_REG_STATUS, ST25R200_REG_STATUS_gpt_on, ST25R200_REG_STATUS_gpt_on )

/*! Checks if Transmitter is enabled (Field On) */
#define st25r200IsTxEnabled()        st25r200CheckReg( ST25R200_REG_OPERATION, ST25R200_REG_OPERATION_tx_en, ST25R200_REG_OPERATION_tx_en )

/*! Checks if NRT is in EMV mode */
#define st25r200IsNRTinEMV()         st25r200CheckReg( ST25R200_REG_NRT_GPT_CONF, ST25R200_REG_NRT_GPT_CONF_nrt_emd, ST25R200_REG_NRT_GPT_CONF_nrt_emd_on )

/*! Checks if last FIFO byte is complete */
#define st25r200IsLastFIFOComplete() st25r200CheckReg( ST25R200_REG_FIFO_STATUS2, ST25R200_REG_FIFO_STATUS2_fifo_lb_mask, 0 )

/*! Checks if the Oscillator is enabled  */
#define st25r200IsOscOn()            st25r200CheckReg( ST25R200_REG_OPERATION, ST25R200_REG_OPERATION_en, ST25R200_REG_OPERATION_en )

/*! Checks if Transmitter (Field On) is enabled */
#define st25r200IsTxOn()             st25r200CheckReg( ST25R200_REG_OPERATION, ST25R200_REG_OPERATION_tx_en, ST25R200_REG_OPERATION_tx_en )

/*
******************************************************************************
* GLOBAL FUNCTION PROTOTYPES
******************************************************************************
*/


#endif /* ST25R200_H */

/**
  * @}
  *
  * @}
  *
  * @}
  *
  * @}
  */

