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
 *  \brief RF Abstraction Layer (RFAL)
 *
 *  RFAL implementation for ST25R3911
 */


/*
******************************************************************************
* INCLUDES
******************************************************************************
*/

#include "rfal_rfst25r200.h"

/*******************************************************************************/
RfalRfST25R200Class::RfalRfST25R200Class(SPIClass *spi, int cs_pin, int int_pin, int reset_pin, uint32_t spi_speed) : dev_spi(spi), cs_pin(cs_pin), int_pin(int_pin), reset_pin(reset_pin), spi_speed(spi_speed)
{
  memset(&gRFAL, 0, sizeof(rfal));
  memset(&gRfalAnalogConfigMgmt, 0, sizeof(rfalAnalogConfigMgmt));
  memset(&rfalIso15693PhyConfig, 0, sizeof(rfalIso15693PhyConfig_t));
  gST25R200NRT_64fcs = 0;
  memset((void *)&st25r200interrupt, 0, sizeof(st25r200Interrupt));
  timerStopwatchTick = 0;
  isr_pending = false;
  irq_handler = NULL;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalInitialize(void)
{
  ReturnCode err;

  if (reset_pin > 0) {
    pinMode(reset_pin, OUTPUT);
    digitalWrite(reset_pin, LOW);
  }

  pinMode(cs_pin, OUTPUT);
  digitalWrite(cs_pin, HIGH);

  pinMode(int_pin, INPUT);
  Callback<void()>::func = std::bind(&RfalRfST25R200Class::st25r200Isr, this);
  irq_handler = static_cast<ST25R200IrqHandler>(Callback<void()>::callback);
  attachInterrupt(int_pin, irq_handler, RISING);

  rfalAnalogConfigInitialize();              /* Initialize RFAL's Analog Configs */

  EXIT_ON_ERR(err, st25r200Initialize());

  st25r200ClearInterrupts();

  /* Disable any previous observation mode */
  rfalST25R200ObsModeDisable();

  /*******************************************************************************/
  /* Apply RF Chip generic initialization */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_INIT));

  /* Clear FIFO status local copy */
  rfalFIFOStatusClear();


  /*******************************************************************************/
  gRFAL.state              = RFAL_STATE_INIT;
  gRFAL.mode               = RFAL_MODE_NONE;
  gRFAL.field              = false;

  /* Set RFAL default configs */
  gRFAL.conf.obsvModeRx    = RFAL_OBSMODE_DISABLE;
  gRFAL.conf.obsvModeTx    = RFAL_OBSMODE_DISABLE;
  gRFAL.conf.eHandling     = ERRORHANDLING_NONE;

  /* Transceive set to IDLE */
  gRFAL.TxRx.lastState     = RFAL_TXRX_STATE_IDLE;
  gRFAL.TxRx.state         = RFAL_TXRX_STATE_IDLE;

  /* Disable all timings */
  gRFAL.timings.FDTListen  = RFAL_TIMING_NONE;
  gRFAL.timings.FDTPoll    = RFAL_TIMING_NONE;
  gRFAL.timings.GT         = RFAL_TIMING_NONE;

  gRFAL.tmr.GT             = RFAL_TIMING_NONE;

  gRFAL.callbacks.preTxRx  = NULL;
  gRFAL.callbacks.postTxRx = NULL;
  gRFAL.callbacks.syncTxRx = NULL;

#if RFAL_FEATURE_WAKEUP_MODE
  /* Initialize Wake-Up Mode */
  gRFAL.wum.state = RFAL_WUM_STATE_NOT_INIT;
#endif /* RFAL_FEATURE_WAKEUP_MODE */

#if RFAL_FEATURE_LOWPOWER_MODE
  /* Initialize Low Power Mode */
  gRFAL.lpm.isRunning     = false;
#endif /* RFAL_FEATURE_LOWPOWER_MODE */


  /*******************************************************************************/
  /* Perform Automatic Calibration (if configured to do so).                     *
   * Registers set by rfalSetAnalogConfig will tell rfalCalibrate what to perform*/
  /* PRQA S 2987 1 # MISRA 2.2 - Feature not available - placeholder  */
  rfalCalibrate();

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalCalibrate(void)
{
  /*******************************************************************************/
  /* Perform ST25R200 regulators calibration                                     */
  /*******************************************************************************/

  /* Automatic regulator adjustment only performed if not set manually on Analog Configs */
  if (st25r200CheckReg(ST25R200_REG_GENERAL, ST25R200_REG_GENERAL_reg_s, 0x00)) {
    return rfalAdjustRegulators(NULL);
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalAdjustRegulators(uint16_t *result)
{
  /* Adjust the regulators with both Tx and Rx enabled for a realistic RW load conditions */
  st25r200TxRxOn();
  st25r200AdjustRegulators(ST25R200_REG_DROP_DO_NOT_SET, result);
  st25r200TxRxOff();

  return ERR_NONE;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetUpperLayerCallback(rfalUpperLayerCallback pFunc)
{
  st25r200IRQCallbackSet(pFunc);
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetPreTxRxCallback(rfalPreTxRxCallback pFunc)
{
  gRFAL.callbacks.preTxRx = pFunc;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetSyncTxRxCallback(rfalSyncTxRxCallback pFunc)
{
  gRFAL.callbacks.syncTxRx = pFunc;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetPostTxRxCallback(rfalPostTxRxCallback pFunc)
{
  gRFAL.callbacks.postTxRx = pFunc;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetLmEonCallback(rfalLmEonCallback pFunc)
{
  NO_WARNING(pFunc);
  return;   /* ERR_NOTSUPP */
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalDeinitialize(void)
{
  /* Deinitialize chip */
  st25r200Deinitialize();

  /* Set Analog configurations for deinitialization */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_DEINIT));

  gRFAL.state = RFAL_STATE_IDLE;
  return ERR_NONE;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetObsvMode(uint32_t txMode, uint32_t rxMode)
{
  gRFAL.conf.obsvModeTx = (uint16_t)txMode;
  gRFAL.conf.obsvModeRx = (uint16_t)rxMode;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalGetObsvMode(uint8_t *txMode, uint8_t *rxMode)
{
  if (txMode != NULL) {
    *txMode = (uint8_t)gRFAL.conf.obsvModeTx;
  }

  if (rxMode != NULL) {
    *rxMode = (uint8_t)gRFAL.conf.obsvModeRx;
  }
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalDisableObsvMode(void)
{
  gRFAL.conf.obsvModeTx = RFAL_OBSMODE_DISABLE;
  gRFAL.conf.obsvModeRx = RFAL_OBSMODE_DISABLE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalSetMode(rfalMode mode, rfalBitRate txBR, rfalBitRate rxBR)
{

  /* Check if RFAL is not initialized */
  if (gRFAL.state == RFAL_STATE_IDLE) {
    return ERR_WRONG_STATE;
  }

  /* Check allowed bit rate value */
  if ((txBR == RFAL_BR_KEEP) || (rxBR == RFAL_BR_KEEP)) {
    return ERR_PARAM;
  }

  switch (mode) {
    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCA:

      /* Enable ISO14443A mode */
      st25r200WriteRegister(ST25R200_REG_PROTOCOL, ST25R200_REG_PROTOCOL_om_iso14443a);

      /* Set Analog configurations for this mode and bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCA_T1T:

      /* Enable Topaz mode */
      st25r200WriteRegister(ST25R200_REG_PROTOCOL, ST25R200_REG_PROTOCOL_om_topaz);

      /* Set Analog configurations for this mode and bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCB:

      /* Enable ISO14443B mode */
      st25r200WriteRegister(ST25R200_REG_PROTOCOL, ST25R200_REG_PROTOCOL_om_iso14443b);

      /* Set Tx SOF and EOF */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_TX2,
                                 (ST25R200_REG_PROTOCOL_TX2_b_tx_sof_mask | ST25R200_REG_PROTOCOL_TX2_b_tx_eof),
                                 (ST25R200_REG_PROTOCOL_TX2_b_tx_sof_0_10etu | ST25R200_REG_PROTOCOL_TX2_b_tx_sof_1_2etu | ST25R200_REG_PROTOCOL_TX2_b_tx_eof_10etu));

      /* Set Rx SOF and EOF */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_RX1,
                                 (ST25R200_REG_PROTOCOL_RX1_b_rx_sof | ST25R200_REG_PROTOCOL_RX1_b_rx_eof),
                                 (ST25R200_REG_PROTOCOL_RX1_b_rx_sof | ST25R200_REG_PROTOCOL_RX1_b_rx_eof));

      /* Set the minimum TR1 (excluding start_wait) */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_RX2, ST25R200_REG_PROTOCOL_RX2_tr1_min_len_mask, RFAL_TR1MIN);

      /* Set Analog configurations for this mode and bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_B_PRIME:

      /* Enable ISO14443B mode */
      st25r200WriteRegister(ST25R200_REG_PROTOCOL, ST25R200_REG_PROTOCOL_om_iso14443b);

      /* Set Tx SOF, EOF and EOF */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_TX2,
                                 (ST25R200_REG_PROTOCOL_TX2_b_tx_sof_mask | ST25R200_REG_PROTOCOL_TX2_b_tx_eof),
                                 (ST25R200_REG_PROTOCOL_TX2_b_tx_sof_0_10etu | ST25R200_REG_PROTOCOL_TX2_b_tx_sof_1_2etu | ST25R200_REG_PROTOCOL_TX2_b_tx_eof_10etu));

      /* Set Rx SOF and EOF */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_RX1,
                                 (ST25R200_REG_PROTOCOL_RX1_b_rx_sof | ST25R200_REG_PROTOCOL_RX1_b_rx_eof),
                                 (ST25R200_REG_PROTOCOL_RX1_b_rx_eof));

      /* Set the minimum TR1 (excluding start_wait) */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_RX2, ST25R200_REG_PROTOCOL_RX2_tr1_min_len_mask, RFAL_TR1MIN);


      /* Set Analog configurations for this mode and bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_B_CTS:

      /* Enable ISO14443B mode */
      st25r200WriteRegister(ST25R200_REG_PROTOCOL, ST25R200_REG_PROTOCOL_om_iso14443b);

      /* Set Tx SOF and EOF */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_TX2,
                                 (ST25R200_REG_PROTOCOL_TX2_b_tx_sof_mask | ST25R200_REG_PROTOCOL_TX2_b_tx_eof),
                                 (ST25R200_REG_PROTOCOL_TX2_b_tx_sof_0_10etu | ST25R200_REG_PROTOCOL_TX2_b_tx_sof_1_2etu | ST25R200_REG_PROTOCOL_TX2_b_tx_eof_10etu));

      /* Set Rx SOF EOF */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_RX1,
                                 (ST25R200_REG_PROTOCOL_RX1_b_rx_sof | ST25R200_REG_PROTOCOL_RX1_b_rx_eof),
                                 0x00);

      /* Set the minimum TR1 (excluding start_wait) */
      st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_RX2, ST25R200_REG_PROTOCOL_RX2_tr1_min_len_mask, RFAL_TR1MIN);

      /* Set Analog configurations for this mode and bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCV:
    case RFAL_MODE_POLL_PICOPASS:

      /* Enable ISO15693 mode */
      st25r200WriteRegister(ST25R200_REG_PROTOCOL, ST25R200_REG_PROTOCOL_om_iso15693);

      /* Set Analog configurations for this mode and bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCF:
    case RFAL_MODE_POLL_ACTIVE_P2P:
    case RFAL_MODE_LISTEN_ACTIVE_P2P:
    case RFAL_MODE_LISTEN_NFCA:
    case RFAL_MODE_LISTEN_NFCF:
    case RFAL_MODE_LISTEN_NFCB:
      return ERR_NOTSUPP;

    /*******************************************************************************/
    default:
      return ERR_NOT_IMPLEMENTED;
  }

  /* Set state as STATE_MODE_SET only if not initialized yet (PSL) */
  gRFAL.state = ((gRFAL.state < RFAL_STATE_MODE_SET) ? RFAL_STATE_MODE_SET : gRFAL.state);
  gRFAL.mode  = mode;

  /* Apply the given bit rate */
  return rfalSetBitRate(txBR, rxBR);
}


/*******************************************************************************/
rfalMode RfalRfST25R200Class::rfalGetMode(void)
{
  return gRFAL.mode;
}

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalSetBitRate(rfalBitRate txBR, rfalBitRate rxBR)
{
  ReturnCode ret;

  /* Check if RFAL is not initialized */
  if (gRFAL.state == RFAL_STATE_IDLE) {
    return ERR_WRONG_STATE;
  }


  if (((txBR != RFAL_BR_KEEP) && (txBR != RFAL_BR_106) && (txBR != RFAL_BR_26p48))                          ||
      ((rxBR != RFAL_BR_KEEP) && (rxBR != RFAL_BR_106) && (rxBR != RFAL_BR_26p48) && (rxBR != RFAL_BR_52p97))) {
    return ERR_PARAM;
  }

  /* Store the new Bit Rates */
  gRFAL.txBR = ((txBR == RFAL_BR_KEEP) ? gRFAL.txBR : txBR);
  gRFAL.rxBR = ((rxBR == RFAL_BR_KEEP) ? gRFAL.rxBR : rxBR);


  /* Set bit rate register */
  EXIT_ON_ERR(ret, st25r200SetBitrate((uint8_t)rfalConvBitRate(gRFAL.txBR), (uint8_t)rfalConvBitRate(gRFAL.rxBR)));


  switch (gRFAL.mode) {
    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCA:
    case RFAL_MODE_POLL_NFCA_T1T:

      /* Set Analog configurations for this bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_POLL_COMMON));
      rfalSetAnalogConfig((rfalAnalogConfigId)(RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((rfalAnalogConfigId)(RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCB:
    case RFAL_MODE_POLL_B_PRIME:
    case RFAL_MODE_POLL_B_CTS:

      /* Set Analog configurations for this bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_POLL_COMMON));
      rfalSetAnalogConfig((rfalAnalogConfigId)(RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((rfalAnalogConfigId)(RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCB | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCV:
    case RFAL_MODE_POLL_PICOPASS:

      /* Set Analog configurations for this bit rate */
      rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_POLL_COMMON));
      rfalSetAnalogConfig((rfalAnalogConfigId)(RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX));
      rfalSetAnalogConfig((rfalAnalogConfigId)(RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX));
      break;

    /*******************************************************************************/
    case RFAL_MODE_POLL_NFCF:
    case RFAL_MODE_POLL_ACTIVE_P2P:
    case RFAL_MODE_LISTEN_ACTIVE_P2P:
    case RFAL_MODE_LISTEN_NFCA:
    case RFAL_MODE_LISTEN_NFCF:
    case RFAL_MODE_LISTEN_NFCB:
    case RFAL_MODE_NONE:
      return ERR_WRONG_STATE;

    /*******************************************************************************/
    default:
      return ERR_NOT_IMPLEMENTED;
  }

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalGetBitRate(rfalBitRate *txBR, rfalBitRate *rxBR)
{
  if ((gRFAL.state == RFAL_STATE_IDLE) || (gRFAL.mode == RFAL_MODE_NONE)) {
    return ERR_WRONG_STATE;
  }

  if (txBR != NULL) {
    *txBR = gRFAL.txBR;
  }

  if (rxBR != NULL) {
    *rxBR = gRFAL.rxBR;
  }

  return ERR_NONE;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetErrorHandling(rfalEHandling eHandling)
{
  switch (eHandling) {
    case ERRORHANDLING_NONE:
      st25r200ClrRegisterBits(ST25R200_REG_EMD1, ST25R200_REG_EMD1_emd_en);
      break;

    case ERRORHANDLING_EMD:
      /* MISRA 16.4: no empty default statement (in case RFAL_SW_EMD is defined) */
#ifndef RFAL_SW_EMD
#ifdef RFAL_HW_EMD_NFF
      st25r200ModifyRegister(ST25R200_REG_EMD1,
                             (ST25R200_REG_EMD1_emd_thld_mask | ST25R200_REG_EMD1_emd_thld_ff | ST25R200_REG_EMD1_emd_en),
                             ((RFAL_EMVCO_RX_MAXLEN << ST25R200_REG_EMD1_emd_thld_shift) | ST25R200_REG_EMD1_emd_en_on));
#else
      st25r200ModifyRegister(ST25R200_REG_EMD1,
                             (ST25R200_REG_EMD1_emd_thld_mask | ST25R200_REG_EMD1_emd_thld_ff | ST25R200_REG_EMD1_emd_en),
                             ((RFAL_EMVCO_RX_MAXLEN << ST25R200_REG_EMD1_emd_thld_shift) | ST25R200_REG_EMD1_emd_thld_ff | ST25R200_REG_EMD1_emd_en_on));
#endif /* RFAL_HW_EMD_FF */

#endif /* RFAL_SW_EMD */
      break;

    default:
      /* MISRA 16.4: no empty default statement (a comment being enough) */
      break;
  }

  gRFAL.conf.eHandling = eHandling;
}


/*******************************************************************************/
rfalEHandling RfalRfST25R200Class::rfalGetErrorHandling(void)
{
  return gRFAL.conf.eHandling;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetFDTPoll(uint32_t FDTPoll)
{
  gRFAL.timings.FDTPoll = MIN(FDTPoll, RFAL_ST25R200_GPT_MAX_1FC);
}


/*******************************************************************************/
uint32_t RfalRfST25R200Class::rfalGetFDTPoll(void)
{
  return gRFAL.timings.FDTPoll;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetFDTListen(uint32_t FDTListen)
{
  gRFAL.timings.FDTListen = MIN(FDTListen, RFAL_ST25R200_MRT_MAX_1FC);
}


/*******************************************************************************/
uint32_t RfalRfST25R200Class::rfalGetFDTListen(void)
{
  return gRFAL.timings.FDTListen;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalSetGT(uint32_t GT)
{
  gRFAL.timings.GT = MIN(GT, RFAL_ST25R200_GT_MAX_1FC);
}


/*******************************************************************************/
uint32_t RfalRfST25R200Class::rfalGetGT(void)
{
  return gRFAL.timings.GT;
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalIsGTExpired(void)
{
  if (gRFAL.tmr.GT != RFAL_TIMING_NONE) {
    if (!rfalTimerisExpired(gRFAL.tmr.GT)) {
      return false;
    }
  }
  return true;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalFieldOnAndStartGT(void)
{
  ReturnCode ret;

  /* Check if RFAL has been initialized (Oscillator should be running) and also
   * if a direct register access has been performed and left the Oscillator Off */
  if ((!st25r200IsOscOn()) || (gRFAL.state < RFAL_STATE_INIT)) {
    return ERR_WRONG_STATE;
  }

  ret = ERR_NONE;

  /* Set Analog configurations for Field On event */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_FIELD_ON));

  /*******************************************************************************/
  /* Perform collision avoidance and turn field On if not already On */
  if ((!st25r200IsTxEnabled()) || (!gRFAL.field)) {
    st25r200TxRxOn();
    gRFAL.field = st25r200IsTxEnabled();
  }

  /*******************************************************************************/
  /* Start GT timer in case the GT value is set */
  if ((gRFAL.timings.GT != RFAL_TIMING_NONE)) {
    /* Ensure that a SW timer doesn't have a lower value then the minimum  */
    rfalTimerStart(gRFAL.tmr.GT, rfalConv1fcToMs(MAX((gRFAL.timings.GT), RFAL_ST25R200_GT_MIN_1FC)));
  }

  return ret;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalFieldOff(void)
{
  /* Check whether a TxRx is not yet finished */
  if (gRFAL.TxRx.state != RFAL_TXRX_STATE_IDLE) {
    rfalCleanupTransceive();
  }

  /* Disable Tx and Rx */
  st25r200TxRxOff();

  /* Set Analog configurations for Field Off event */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_FIELD_OFF));
  gRFAL.field = false;

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalStartTransceive(const rfalTransceiveContext *ctx)
{
  uint32_t FxTAdj;  /* FWT or FDT adjustment calculation */

  /* Check for valid parameters */
  if (ctx == NULL) {
    return ERR_PARAM;
  }

  /* Ensure that RFAL is already Initialized and the mode has been set */
  if (gRFAL.state >= RFAL_STATE_MODE_SET) {
    /*******************************************************************************/
    /* Check whether the field is already On, otherwise no TXE will be received  */
    if ((!st25r200IsTxEnabled()) && (ctx->txBuf != NULL)) {
      return ERR_WRONG_STATE;
    }

    gRFAL.TxRx.ctx = *ctx;

    /*******************************************************************************/
    if (gRFAL.timings.FDTListen != RFAL_TIMING_NONE) {
      /* Calculate MRT adjustment accordingly to the current mode */
      FxTAdj = RFAL_FDT_LISTEN_MRT_ADJUSTMENT;
      if (gRFAL.mode == RFAL_MODE_POLL_NFCA)      {
        FxTAdj += (uint32_t)RFAL_FDT_LISTEN_A_ADJUSTMENT;
      }
      if (gRFAL.mode == RFAL_MODE_POLL_NFCA_T1T)  {
        FxTAdj += (uint32_t)RFAL_FDT_LISTEN_A_ADJUSTMENT;
      }
      if (gRFAL.mode == RFAL_MODE_POLL_NFCB)      {
        FxTAdj += (uint32_t)RFAL_FDT_LISTEN_B_ADJUSTMENT;
      }
      if (gRFAL.mode == RFAL_MODE_POLL_NFCV)      {
        FxTAdj += (uint32_t)RFAL_FDT_LISTEN_V_ADJUSTMENT;
      }

      /* Ensure that MRT is using 64/fc steps */
      st25r200ChangeRegisterBits(ST25R200_REG_MRT_SQT_CONF, ST25R200_REG_MRT_SQT_CONF_mrt_step_mask, ST25R200_REG_MRT_SQT_CONF_mrt_step_64fc);

      /* Set Minimum FDT(Listen) in which PICC is not allowed to send a response */
      st25r200WriteRegister(ST25R200_REG_MRT, (uint8_t)rfalConv1fcTo64fc((FxTAdj > gRFAL.timings.FDTListen) ? RFAL_ST25R200_MRT_MIN_1FC : (gRFAL.timings.FDTListen - FxTAdj)));
    }

    /*******************************************************************************/
    /* FDT Poll will be loaded in rfalPrepareTransceive() once the previous was expired */

    /*******************************************************************************/
    if ((gRFAL.TxRx.ctx.fwt != RFAL_FWT_NONE) && (gRFAL.TxRx.ctx.fwt != 0U)) {
      /* Ensure proper timing configuration */
      if (gRFAL.timings.FDTListen >= gRFAL.TxRx.ctx.fwt) {
        return ERR_PARAM;
      }

      FxTAdj = RFAL_FWT_ADJUSTMENT;
      if (gRFAL.mode == RFAL_MODE_POLL_NFCA)      {
        FxTAdj += (uint32_t)RFAL_FWT_A_ADJUSTMENT;
      }
      if (gRFAL.mode == RFAL_MODE_POLL_NFCA_T1T)  {
        FxTAdj += (uint32_t)RFAL_FWT_A_ADJUSTMENT;
      }
      if (gRFAL.mode == RFAL_MODE_POLL_NFCB)      {
        FxTAdj += (uint32_t)RFAL_FWT_B_ADJUSTMENT;
      }
      if (gRFAL.mode == RFAL_MODE_POLL_NFCV)      {
        FxTAdj += (uint32_t)RFAL_FWT_V_ADJUSTMENT;
      }

      /* Ensure that the given FWT doesn't exceed NRT maximum */
      gRFAL.TxRx.ctx.fwt = MIN((gRFAL.TxRx.ctx.fwt + FxTAdj), RFAL_ST25R200_NRT_MAX_1FC);

      /* Set FWT in the NRT */
      st25r200SetNoResponseTime(rfalConv1fcTo64fc(gRFAL.TxRx.ctx.fwt));
    } else {
      /* Disable NRT, no NRE will be triggered, therefore wait endlessly for Rx */
      st25r200SetNoResponseTime(RFAL_ST25R200_NRT_DISABLED);
    }

    gRFAL.state       = RFAL_STATE_TXRX;
    gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_IDLE;
    gRFAL.TxRx.status = ERR_BUSY;


    /*******************************************************************************/
    /* Check if the Transceive start performing Tx or goes directly to Rx          */
    if ((gRFAL.TxRx.ctx.txBuf == NULL) || (gRFAL.TxRx.ctx.txBufLen == 0U)) {
      /* Clear FIFO, Clear and Enable the Interrupts */
      rfalPrepareTransceive();

      /* No Tx done, enable the Receiver */
      st25r200ExecuteCommand(ST25R200_CMD_UNMASK_RECEIVE_DATA);

      /* Start NRT manually, if FWT = 0 (wait endlessly for Rx) chip will ignore anyhow */
      st25r200ExecuteCommand(ST25R200_CMD_START_NO_RESPONSE_TIMER);

      gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_IDLE;
    }

    return ERR_NONE;
  }

  return ERR_WRONG_STATE;
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalIsTransceiveInTx(void)
{
  return ((gRFAL.TxRx.state >= RFAL_TXRX_STATE_TX_IDLE) && (gRFAL.TxRx.state < RFAL_TXRX_STATE_RX_IDLE));
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalIsTransceiveInRx(void)
{
  return (gRFAL.TxRx.state >= RFAL_TXRX_STATE_RX_IDLE);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalTransceiveBlockingTx(uint8_t *txBuf, uint16_t txBufLen, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *actLen, uint32_t flags, uint32_t fwt)
{
  ReturnCode               ret;
  rfalTransceiveContext    ctx;

  rfalCreateByteFlagsTxRxContext(ctx, txBuf, txBufLen, rxBuf, rxBufLen, actLen, flags, fwt);
  EXIT_ON_ERR(ret, rfalStartTransceive(&ctx));

  return rfalTransceiveRunBlockingTx();
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalTransceiveRunBlockingTx(void)
{
  ReturnCode ret;

  do {
    rfalWorker();
    ret = rfalGetTransceiveStatus();
  } while ((rfalIsTransceiveInTx()) && (ret == ERR_BUSY));

  if (rfalIsTransceiveInRx()) {
    return ERR_NONE;
  }

  return ret;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalTransceiveBlockingRx(void)
{
  ReturnCode ret;

  do {
    rfalWorker();
    ret = rfalGetTransceiveStatus();
  } while ((rfalIsTransceiveInRx()) || (ret == ERR_BUSY));

  return ret;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalTransceiveBlockingTxRx(uint8_t *txBuf, uint16_t txBufLen, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *actLen, uint32_t flags, uint32_t fwt)
{
  ReturnCode ret;

  EXIT_ON_ERR(ret, rfalTransceiveBlockingTx(txBuf, txBufLen, rxBuf, rxBufLen, actLen, flags, fwt));
  ret = rfalTransceiveBlockingRx();

  /* Convert received bits to bytes */
  if (actLen != NULL) {
    *actLen = rfalConvBitsToBytes(*actLen);
  }

  return ret;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalRunTransceiveWorker(void)
{
  if (gRFAL.state == RFAL_STATE_TXRX) {
    /* Run Tx or Rx state machines */
    if (rfalIsTransceiveInTx()) {
      rfalTransceiveTx();
      return rfalGetTransceiveStatus();
    }
    if (rfalIsTransceiveInRx()) {
      rfalTransceiveRx();
      return rfalGetTransceiveStatus();
    }
  }
  return ERR_WRONG_STATE;
}


/*******************************************************************************/
rfalTransceiveState RfalRfST25R200Class::rfalGetTransceiveState(void)
{
  return gRFAL.TxRx.state;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalGetTransceiveStatus(void)
{
  return ((gRFAL.TxRx.state == RFAL_TXRX_STATE_IDLE) ? gRFAL.TxRx.status : ERR_BUSY);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalGetTransceiveRSSI(uint16_t *rssi)
{
  if (rssi != NULL) {
    (*rssi) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalIsTransceiveSubcDetected(void)
{
  if ((gRFAL.state == RFAL_STATE_TXRX) || (gRFAL.TxRx.state == RFAL_TXRX_STATE_IDLE)) {
    return (st25r200GetInterrupt(ST25R200_IRQ_MASK_SUBC_START) != 0U);
  }
  return false;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalWorker(void)
{
  // platformProtectWorker();               /* Protect RFAL Worker/Task/Process */

#ifdef ST25R_POLL_IRQ
  st25r200CheckForReceivedInterrupts();
#endif /* ST25R_POLL_IRQ */

  switch (gRFAL.state) {
    case RFAL_STATE_TXRX:
      rfalRunTransceiveWorker();
      break;

#if RFAL_FEATURE_WAKEUP_MODE
    case RFAL_STATE_WUM:
      rfalRunWakeUpModeWorker();
      break;
#endif /* RFAL_FEATURE_WAKEUP_MODE */

    /* Nothing to be done */
    default:
      /* MISRA 16.4: no empty default statement (a comment being enough) */
      break;
  }

  // platformUnprotectWorker();             /* Unprotect RFAL Worker/Task/Process */
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalErrorHandling(void)
{
  uint16_t fifoBytesToRead;

  fifoBytesToRead = rfalFIFOStatusGetNumBytes();


#ifdef RFAL_SW_EMD
  /*******************************************************************************/
  /* EMD                                                                         */
  /*******************************************************************************/
  if (gRFAL.conf.eHandling == ERRORHANDLING_EMD) {
    bool    rxHasIncParError;

    /*******************************************************************************/
    /* EMD Handling - NFC Forum Digital 1.1  4.1.1.1 ; EMVCo v2.5  4.9.2           */
    /* Re-enable the receiver on frames with a length < 4 bytes, upon:              */
    /*   - Collision or Framing error detected                                     */
    /*   - Residual bits are detected (hard framing error)                         */
    /*   - Parity error                                                            */
    /*   - CRC error                                                               */
    /*******************************************************************************/

    /* Check if reception has incomplete bytes or parity error */
    rxHasIncParError = (rfalFIFOStatusIsIncompleteByte() ? true : rfalFIFOStatusIsMissingPar());     /* MISRA 13.5 */


    /* In case there are residual bits decrement FIFO bytes */
    /* Ensure FIFO contains some byte as the FIFO might be empty upon Framing errors */
    if ((fifoBytesToRead > 0U) && rxHasIncParError) {
      fifoBytesToRead--;
    }

    if (((gRFAL.fifo.bytesTotal + fifoBytesToRead) < RFAL_EMVCO_RX_MAXLEN)            &&
        ((gRFAL.TxRx.status == ERR_RF_COLLISION) || (gRFAL.TxRx.status == ERR_FRAMING) ||
         (gRFAL.TxRx.status == ERR_PAR)          || (gRFAL.TxRx.status == ERR_CRC)     ||
         rxHasIncParError)) {
      /* Ignore this reception, Re-enable receiver which also clears the FIFO */
      st25r200ExecuteCommand(ST25R200_CMD_UNMASK_RECEIVE_DATA);

      /* Ensure that the NRT has not expired meanwhile */
      if (st25r200CheckReg(ST25R200_REG_STATUS, ST25R200_REG_STATUS_nrt_on, 0x00)) {
        if (st25r200CheckReg(ST25R200_REG_STATUS, ST25R200_REG_STATUS_rx_act, 0x00)) {
          /* Abort reception */
          st25r200ExecuteCommand(ST25R200_CMD_MASK_RECEIVE_DATA);
          gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
          return;
        }
      }

      rfalFIFOStatusClear();
      gRFAL.fifo.bytesTotal = 0;
      gRFAL.TxRx.status     = ERR_BUSY;
      gRFAL.TxRx.state      = RFAL_TXRX_STATE_RX_WAIT_RXS;
    }
    return;
  }
#endif /* RFAL_SW_EMD */


  /*******************************************************************************/
  /* ISO14443A Mode                                                              */
  /*******************************************************************************/
  if (gRFAL.mode == RFAL_MODE_POLL_NFCA) {
    /*******************************************************************************/
    /* If we received a frame with a incomplete byte we`ll raise a specific error  *
     * ( support for T2T 4 bit ACK / NAK, MIFARE and Kovio )                       */
    /*******************************************************************************/
    if ((gRFAL.TxRx.status == ERR_PAR) || (gRFAL.TxRx.status == ERR_CRC)) {
      if ((rfalFIFOStatusIsIncompleteByte()) && (fifoBytesToRead == RFAL_RX_INC_BYTE_LEN)) {
        st25r200ReadFifo((uint8_t *)(gRFAL.TxRx.ctx.rxBuf), fifoBytesToRead);
        if ((gRFAL.TxRx.ctx.rxRcvdLen) != NULL) {
          *gRFAL.TxRx.ctx.rxRcvdLen = rfalFIFOGetNumIncompleteBits();
        }

        gRFAL.TxRx.status = ERR_INCOMPLETE_BYTE;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
      }
    }
  }
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalCleanupTransceive(void)
{
  /*******************************************************************************/
  /* Transceive flags                                                            */
  /*******************************************************************************/

  /* Restore default settings on Tx Parity and CRC */
  st25r200SetRegisterBits(ST25R200_REG_PROTOCOL_TX1, (ST25R200_REG_PROTOCOL_TX1_a_tx_par | ST25R200_REG_PROTOCOL_TX1_tx_crc));

  /* Restore default settings on Receiving parity + CRC bits */
  st25r200SetRegisterBits(ST25R200_REG_PROTOCOL_RX1, (ST25R200_REG_PROTOCOL_RX1_a_rx_par | ST25R200_REG_PROTOCOL_RX1_rx_crc));

  /* Restore AGC enabled */
  st25r200SetRegisterBits(ST25R200_REG_RX_DIG, ST25R200_REG_RX_DIG_agc_en);

  /*******************************************************************************/
  /* Execute Post Transceive Callback                                            */
  /*******************************************************************************/
  if (gRFAL.callbacks.postTxRx != NULL) {
    gRFAL.callbacks.postTxRx();
  }
  /*******************************************************************************/
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalPrepareTransceive(void)
{
  uint32_t maskInterrupts;
  uint32_t clrMaskInterrupts;
  uint8_t  reg;

  /* Reset receive logic with STOP command */
  st25r200ExecuteCommand(ST25R200_CMD_STOP);

  /* Reset Rx Gain */
  st25r200ExecuteCommand(ST25R200_CMD_CLEAR_RXGAIN);

  /*******************************************************************************/
  /* FDT Poll                                                                    */
  /*******************************************************************************/
  /* In Passive communications General Purpose Timer is used to measure FDT Poll */
  if (gRFAL.timings.FDTPoll != RFAL_TIMING_NONE) {
    /* Configure GPT to start at RX end */
    st25r200SetStartGPTimer((uint16_t)rfalConv1fcTo8fc(((gRFAL.timings.FDTPoll < RFAL_FDT_POLL_ADJUSTMENT) ? gRFAL.timings.FDTPoll : (gRFAL.timings.FDTPoll - RFAL_FDT_POLL_ADJUSTMENT))), ST25R200_REG_NRT_GPT_CONF_gptc_erx);
  }

  /*******************************************************************************/
  /* Execute Pre Transceive Callback                                             */
  /*******************************************************************************/
  if (gRFAL.callbacks.preTxRx != NULL) {
    gRFAL.callbacks.preTxRx();
  }
  /*******************************************************************************/

  /* IRQs that require immediate serving (ISR) for TxRx */
  maskInterrupts = (ST25R200_IRQ_MASK_WL   | ST25R200_IRQ_MASK_TXE  |
                    ST25R200_IRQ_MASK_RXS  | ST25R200_IRQ_MASK_RXE  |
                    ST25R200_IRQ_MASK_PAR  | ST25R200_IRQ_MASK_CRC  |
                    ST25R200_IRQ_MASK_HFE  | ST25R200_IRQ_MASK_SFE  |
                    ST25R200_IRQ_MASK_NRE);

  /* IRQs that do not require immediate serving but may be used for TxRx */
  clrMaskInterrupts = ST25R200_IRQ_MASK_SUBC_START;

  /*******************************************************************************/
  /* Transceive flags                                                            */
  /*******************************************************************************/
  /* Transmission Flags */
  reg = (ST25R200_REG_PROTOCOL_TX1_tx_crc_on | ST25R200_REG_PROTOCOL_TX1_a_tx_par_on);


  /* Check if automatic Parity bits is to be disabled */
  if ((gRFAL.TxRx.ctx.flags & (uint8_t)RFAL_TXRX_FLAGS_PAR_TX_NONE) != 0U) {
    reg &= ~ST25R200_REG_PROTOCOL_TX1_a_tx_par;
  }

  /* Check if automatic Parity bits is to be disabled */
  if ((gRFAL.TxRx.ctx.flags & (uint8_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL) != 0U) {
    reg &= ~ST25R200_REG_PROTOCOL_TX1_tx_crc;
  }

  /* Apply current TxRx flags on Tx Register */
  st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_TX1, (ST25R200_REG_PROTOCOL_TX1_tx_crc | ST25R200_REG_PROTOCOL_TX1_a_tx_par), reg);


  /* Check if NFCIP1 mode is to be enabled */
  if ((gRFAL.TxRx.ctx.flags & (uint8_t)RFAL_TXRX_FLAGS_NFCIP1_ON) != 0U) {
    /* No NFCIP1 HW handling: ignore */
  }


  /* Reception Flags */
  reg = (ST25R200_REG_PROTOCOL_RX1_a_rx_par_on | ST25R200_REG_PROTOCOL_RX1_rx_crc_on);

  /* Check if Parity check is to be skipped and to keep the parity bits in FIFO */
  if ((gRFAL.TxRx.ctx.flags & (uint8_t)RFAL_TXRX_FLAGS_PAR_RX_KEEP) != 0U) {
    reg &= ~ST25R200_REG_PROTOCOL_RX1_a_rx_par;
  }

  /* Check if CRC check is to be skipped */
  if ((gRFAL.TxRx.ctx.flags & (uint8_t)RFAL_TXRX_FLAGS_CRC_RX_MANUAL) != 0U) {
    reg &= ~ST25R200_REG_PROTOCOL_RX1_rx_crc;
  }

  /* Apply current TxRx flags on Tx Register */
  st25r200ChangeRegisterBits(ST25R200_REG_PROTOCOL_RX1, (ST25R200_REG_PROTOCOL_RX1_a_rx_par | ST25R200_REG_PROTOCOL_RX1_rx_crc), reg);


  /* Check if AGC is to be disabled */
  if ((gRFAL.TxRx.ctx.flags & (uint8_t)RFAL_TXRX_FLAGS_AGC_OFF) != 0U) {
    st25r200ClrRegisterBits(ST25R200_REG_RX_DIG, ST25R200_REG_RX_DIG_agc_en);
  } else {
    st25r200SetRegisterBits(ST25R200_REG_RX_DIG, ST25R200_REG_RX_DIG_agc_en);
  }
  /*******************************************************************************/


  /*******************************************************************************/
  /* EMD NRT mode                                                              */
  /*******************************************************************************/
  if (gRFAL.conf.eHandling == ERRORHANDLING_EMD) {
    st25r200SetRegisterBits(ST25R200_REG_NRT_GPT_CONF, ST25R200_REG_NRT_GPT_CONF_nrt_emd);
    maskInterrupts |= ST25R200_IRQ_MASK_RX_REST;
  } else {
    st25r200ClrRegisterBits(ST25R200_REG_NRT_GPT_CONF, ST25R200_REG_NRT_GPT_CONF_nrt_emd);
  }
  /*******************************************************************************/


  /*******************************************************************************/
  /* Clear and enable these interrupts */
  st25r200GetInterrupt((maskInterrupts | clrMaskInterrupts));
  st25r200EnableInterrupts(maskInterrupts);

  /* Clear FIFO status local copy */
  rfalFIFOStatusClear();
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalTransceiveTx(void)
{
  volatile uint32_t irqs;
  uint16_t          tmp;
  ReturnCode        ret;

  /* Suppress warning in case NFC-V feature is disabled */
  ret = ERR_NONE;
  NO_WARNING(ret);

  irqs = ST25R200_IRQ_MASK_NONE;

  if (gRFAL.TxRx.state != gRFAL.TxRx.lastState) {
    gRFAL.TxRx.lastState = gRFAL.TxRx.state;
  }

  switch (gRFAL.TxRx.state) {
    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_IDLE:

      /* Nothing to do */
      gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_WAIT_GT ;
    /* fall through */

    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_WAIT_GT:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      if (!rfalIsGTExpired()) {
        break;
      }

      gRFAL.tmr.GT = RFAL_TIMING_NONE;

      gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_WAIT_FDT;
    /* fall through */

    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_WAIT_FDT:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      /* In Passive communications GPT is used to measure FDT Poll */
      if (st25r200IsGPTRunning()) {
        break;
      }

      gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_PREP_TX;
    /* fall through */

    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_PREP_TX:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      /* Clear FIFO, Clear and Enable the Interrupts */
      rfalPrepareTransceive();

      /* ST25R200 has a fixed FIFO water level */
      gRFAL.fifo.expWL = RFAL_FIFO_OUT_WL;

      /* Calculate the bytes needed to be Written into FIFO (a incomplete byte will be added as 1byte) */
      gRFAL.fifo.bytesTotal = (uint16_t)rfalCalcNumBytes(gRFAL.TxRx.ctx.txBufLen);

      /* Set the number of full bytes and bits to be transmitted */
      st25r200SetNumTxBits(gRFAL.TxRx.ctx.txBufLen);

      /* Load FIFO with total length or FIFO's maximum */
      gRFAL.fifo.bytesWritten = MIN(gRFAL.fifo.bytesTotal, ST25R200_FIFO_DEPTH);
      st25r200WriteFifo(gRFAL.TxRx.ctx.txBuf, gRFAL.fifo.bytesWritten);

      /*Check if Observation Mode is enabled and set it on ST25R391x */
      rfalCheckEnableObsModeTx();

      gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_TRANSMIT;
    /* fall through */


    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_TRANSMIT:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      /*******************************************************************************/
      /* Execute Sync Transceive Callback                                             */
      /*******************************************************************************/
      if (gRFAL.callbacks.syncTxRx != NULL) {
        /* If set, wait for sync callback to signal sync/trigger transmission */
        if (!gRFAL.callbacks.syncTxRx()) {
          break;
        }
      }

      /*******************************************************************************/
      /* Trigger/Start transmission                                                  */
      st25r200ExecuteCommand(ST25R200_CMD_TRANSMIT);

      /* Check if a WL level is expected or TXE should come */
      gRFAL.TxRx.state = ((gRFAL.fifo.bytesWritten < gRFAL.fifo.bytesTotal) ? RFAL_TXRX_STATE_TX_WAIT_WL : RFAL_TXRX_STATE_TX_WAIT_TXE);
      break;

    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_WAIT_WL:

      irqs = st25r200GetInterrupt((ST25R200_IRQ_MASK_WL | ST25R200_IRQ_MASK_TXE));
      if (irqs == ST25R200_IRQ_MASK_NONE) {
        break;  /* No interrupt to process */
      }

      if (((irqs & ST25R200_IRQ_MASK_WL) != 0U) && ((irqs & ST25R200_IRQ_MASK_TXE) == 0U)) {
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_RELOAD_FIFO;
      } else {
        gRFAL.TxRx.status = ERR_IO;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_FAIL;
        break;
      }

    /* fall through */

    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_RELOAD_FIFO:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      /* Load FIFO with the remaining length or maximum available */
      tmp = MIN((gRFAL.fifo.bytesTotal - gRFAL.fifo.bytesWritten), gRFAL.fifo.expWL);        /* tmp holds the number of bytes written on this iteration */
      st25r200WriteFifo(&gRFAL.TxRx.ctx.txBuf[gRFAL.fifo.bytesWritten], tmp);

      /* Update total written bytes to FIFO */
      gRFAL.fifo.bytesWritten += tmp;

      /* Check if a WL level is expected or TXE should come */
      gRFAL.TxRx.state = ((gRFAL.fifo.bytesWritten < gRFAL.fifo.bytesTotal) ? RFAL_TXRX_STATE_TX_WAIT_WL : RFAL_TXRX_STATE_TX_WAIT_TXE);
      break;


    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_WAIT_TXE:

      irqs = st25r200GetInterrupt((ST25R200_IRQ_MASK_WL | ST25R200_IRQ_MASK_TXE));
      if (irqs == ST25R200_IRQ_MASK_NONE) {
        break;  /* No interrupt to process */
      }


      if ((irqs & ST25R200_IRQ_MASK_TXE) != 0U) {
        gRFAL.TxRx.state = RFAL_TXRX_STATE_TX_DONE;
      } else if ((irqs & ST25R200_IRQ_MASK_WL) != 0U) {
        break;  /* Ignore ST25R200 FIFO WL if total TxLen is already on the FIFO */
      } else {
        gRFAL.TxRx.status = ERR_IO;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_FAIL;
        break;
      }

    /* fall through */


    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_DONE:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      /* If no rxBuf is provided do not wait/expect Rx */
      if (gRFAL.TxRx.ctx.rxBuf == NULL) {
        /*Check if Observation Mode was enabled and disable it on ST25R391x */
        rfalCheckDisableObsMode();

        /* Clean up Transceive */
        rfalCleanupTransceive();

        gRFAL.TxRx.status = ERR_NONE;
        gRFAL.TxRx.state  =  RFAL_TXRX_STATE_IDLE;
        break;
      }

      rfalCheckEnableObsModeRx();

      /* Goto Rx */
      gRFAL.TxRx.state  =  RFAL_TXRX_STATE_RX_IDLE;
      break;

    /*******************************************************************************/
    case RFAL_TXRX_STATE_TX_FAIL:

      /* Error should be assigned by previous state */
      if (gRFAL.TxRx.status == ERR_BUSY) {
        gRFAL.TxRx.status = ERR_SYSTEM;
      }

      /*Check if Observation Mode was enabled and disable it on ST25R391x */
      rfalCheckDisableObsMode();

      /* Clean up Transceive */
      rfalCleanupTransceive();

      gRFAL.TxRx.state = RFAL_TXRX_STATE_IDLE;
      break;

    /*******************************************************************************/
    default:

      gRFAL.TxRx.status = ERR_SYSTEM;
      gRFAL.TxRx.state  = RFAL_TXRX_STATE_TX_FAIL;
      break;
  }
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalTransceiveRx(void)
{
  volatile uint32_t irqs;
  uint16_t          tmp;
  uint16_t          aux;

  irqs = ST25R200_IRQ_MASK_NONE;

  if (gRFAL.TxRx.state != gRFAL.TxRx.lastState) {
    gRFAL.TxRx.lastState = gRFAL.TxRx.state;
  }

  switch (gRFAL.TxRx.state) {
    /*******************************************************************************/
    case RFAL_TXRX_STATE_RX_IDLE:

      /* Clear rx counters */
      gRFAL.fifo.bytesWritten   = 0;            /* Total bytes written on RxBuffer         */
      gRFAL.fifo.bytesTotal     = 0;            /* Total bytes in FIFO will now be from Rx */
      if (gRFAL.TxRx.ctx.rxRcvdLen != NULL) {
        *gRFAL.TxRx.ctx.rxRcvdLen = 0;
      }

      gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_WAIT_RXS;

    /* fall through */

    /*******************************************************************************/
    case RFAL_TXRX_STATE_RX_WAIT_RXS:    /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      /*******************************************************************************/
      irqs = st25r200GetInterrupt((ST25R200_IRQ_MASK_RXS | ST25R200_IRQ_MASK_NRE));
      if (irqs == ST25R200_IRQ_MASK_NONE) {
        break;  /* No interrupt to process */
      }

      /* Only raise Timeout if NRE is detected with no Rx Start (NRT EMD mode) */
      if (((irqs & ST25R200_IRQ_MASK_NRE) != 0U) && ((irqs & ST25R200_IRQ_MASK_RXS) == 0U)) {
        gRFAL.TxRx.status = ERR_TIMEOUT;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
        break;
      }

      if ((irqs & ST25R200_IRQ_MASK_RXS) != 0U) {
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_WAIT_RXE;
      } else {
        gRFAL.TxRx.status = ERR_IO;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
        break;
      }

      /* Remove NRE that might appear together (NRT EMD mode), and remove RXS */
      irqs &= ~(ST25R200_IRQ_MASK_RXS | ST25R200_IRQ_MASK_NRE);

    /* fall through */

    /*******************************************************************************/
    case RFAL_TXRX_STATE_RX_WAIT_RXE:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      irqs |= st25r200GetInterrupt((ST25R200_IRQ_MASK_RXE  | ST25R200_IRQ_MASK_WL | ST25R200_IRQ_MASK_RX_REST));
      if (irqs == ST25R200_IRQ_MASK_NONE) {
        break;  /* No interrupt to process */
      }

      if ((irqs & ST25R200_IRQ_MASK_RX_REST) != 0U) {
        /* RX_REST indicates that Receiver has been reset due to EMD, therefore a RXS + RXE should *
         * follow if a good reception is followed within the valid initial timeout                   */

        /* Check whether NRT has expired already, if so signal a timeout */
        if (st25r200GetInterrupt(ST25R200_IRQ_MASK_NRE) != 0U) {
          gRFAL.TxRx.status = ERR_TIMEOUT;
          gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
          break;
        }
        if (st25r200CheckReg(ST25R200_REG_STATUS, ST25R200_REG_STATUS_nrt_on, 0)) {    /* MISRA 13.5 */
          gRFAL.TxRx.status = ERR_TIMEOUT;
          gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
          break;
        }

        /* Discard any previous RXS and transmission errors */
        st25r200GetInterrupt((ST25R200_IRQ_MASK_RXS | ST25R200_IRQ_MASK_SFE | ST25R200_IRQ_MASK_HFE | ST25R200_IRQ_MASK_CRC));

        /* Check whether a following reception has already started */
        if (st25r200CheckReg(ST25R200_REG_STATUS, ST25R200_REG_STATUS_rx_act, ST25R200_REG_STATUS_rx_act)) {
          gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_WAIT_RXE;
          break;
        }

        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_WAIT_RXS;
        break;
      }

      if (((irqs & ST25R200_IRQ_MASK_WL) != 0U) && ((irqs & ST25R200_IRQ_MASK_RXE) == 0U)) {
        gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_READ_FIFO;
        break;
      }

      /* After RXE retrieve and check for any error irqs */
      irqs |= st25r200GetInterrupt((ST25R200_IRQ_MASK_CRC | ST25R200_IRQ_MASK_PAR | ST25R200_IRQ_MASK_HFE | ST25R200_IRQ_MASK_SFE | ST25R200_IRQ_MASK_COL));

      gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_ERR_CHECK;
    /* fall through */


    /*******************************************************************************/
    case RFAL_TXRX_STATE_RX_ERR_CHECK:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      if ((irqs & ST25R200_IRQ_MASK_HFE) != 0U) {
        gRFAL.TxRx.status = ERR_FRAMING;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;

        /* Check if there's a specific error handling for this */
        rfalErrorHandling();
        break;
      } else if (((irqs & ST25R200_IRQ_MASK_SFE) != 0U)) {
        gRFAL.TxRx.status = ERR_FRAMING;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;

        /* Check if there's a specific error handling for this */
        rfalErrorHandling();
        break;
      } else if ((irqs & ST25R200_IRQ_MASK_PAR) != 0U) {
        gRFAL.TxRx.status = ERR_PAR;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;

        /* Check if there's a specific error handling for this */
        rfalErrorHandling();
        break;
      } else if ((irqs & ST25R200_IRQ_MASK_COL) != 0U) {
        gRFAL.TxRx.status = ERR_RF_COLLISION;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;

        /* Check if there's a specific error handling for this */
        rfalErrorHandling();
        break;
      } else if ((irqs & ST25R200_IRQ_MASK_CRC) != 0U) {
        gRFAL.TxRx.status = ERR_CRC;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_READ_DATA;

        /* Check if there's a specific error handling for this */
        rfalErrorHandling();
        break;
      } else if ((irqs & ST25R200_IRQ_MASK_RXE) != 0U) {
        /* Reception ended without any error indication,                  *
         * check FIFO status for malformed or incomplete frames           */

        /* Check if the reception ends with an incomplete byte (residual bits) */
        if (rfalFIFOStatusIsIncompleteByte()) {
          gRFAL.TxRx.status = ERR_INCOMPLETE_BYTE;
        }
        /* Check if the reception ends missing parity bit */
        else if (rfalFIFOStatusIsMissingPar()) {
          gRFAL.TxRx.status = ERR_FRAMING;
        } else {
          /* MISRA 15.7 - Empty else */
        }

        gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_READ_DATA;
      } else {
        gRFAL.TxRx.status = ERR_IO;
        gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
        break;
      }

    /* fall through */


    /*******************************************************************************/
    case RFAL_TXRX_STATE_RX_READ_DATA:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      tmp = rfalFIFOStatusGetNumBytes();

      /*******************************************************************************/
      /* Check if CRC should not be placed in rxBuf                                  */
      if (((gRFAL.TxRx.ctx.flags & (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_KEEP) == 0U)) {
        /* if received frame was bigger than CRC */
        if ((uint16_t)(gRFAL.fifo.bytesTotal + tmp) > 0U) {
          /* By default CRC will not be placed into the rxBuffer */
          if ((tmp > RFAL_CRC_LEN)) {
            tmp -= RFAL_CRC_LEN;
          }
          /* If the CRC was already placed into rxBuffer (due to WL interrupt where CRC was already in FIFO Read)
           * cannot remove it from rxBuf. Can only remove it from rxBufLen not indicate the presence of CRC    */
          else if (gRFAL.fifo.bytesTotal > RFAL_CRC_LEN) {
            gRFAL.fifo.bytesTotal -= RFAL_CRC_LEN;
          } else {
            /* MISRA 15.7 - Empty else */
          }
        }
      }

      gRFAL.fifo.bytesTotal += tmp;                    /* add to total bytes counter */

      /*******************************************************************************/
      /* Check if remaining bytes fit on the rxBuf available                         */
      if (gRFAL.fifo.bytesTotal > rfalConvBitsToBytes(gRFAL.TxRx.ctx.rxBufLen)) {
        tmp = (uint16_t)(rfalConvBitsToBytes(gRFAL.TxRx.ctx.rxBufLen) - gRFAL.fifo.bytesWritten);

        /* Transmission errors have precedence over buffer error */
        if (gRFAL.TxRx.status == ERR_BUSY) {
          gRFAL.TxRx.status = ERR_NOMEM;
        }
      }

      /*******************************************************************************/
      /* Retrieve remaining bytes from FIFO to rxBuf, and assign total length rcvd   */
      st25r200ReadFifo(&gRFAL.TxRx.ctx.rxBuf[gRFAL.fifo.bytesWritten], tmp);
      if (gRFAL.TxRx.ctx.rxRcvdLen != NULL) {
        (*gRFAL.TxRx.ctx.rxRcvdLen) = (uint16_t)rfalConvBytesToBits(gRFAL.fifo.bytesTotal);
        if (rfalFIFOStatusIsIncompleteByte()) {
          (*gRFAL.TxRx.ctx.rxRcvdLen) -= (RFAL_BITS_IN_BYTE - rfalFIFOGetNumIncompleteBits());
        }
      }

      /*******************************************************************************/
      /* If an error as been marked/detected don't fall into to RX_DONE  */
      if (gRFAL.TxRx.status != ERR_BUSY) {
        gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_FAIL;
        break;
      }

      gRFAL.TxRx.state = RFAL_TXRX_STATE_RX_DONE;
    /* fall through */


    /*******************************************************************************/
    case RFAL_TXRX_STATE_RX_DONE:   /*  PRQA S 2003 # MISRA 16.3 - Intentional fall through */

      /*Check if Observation Mode was enabled and disable it on ST25R391x */
      rfalCheckDisableObsMode();

      /* Clean up Transceive */
      rfalCleanupTransceive();

      gRFAL.TxRx.status = ERR_NONE;
      gRFAL.TxRx.state  = RFAL_TXRX_STATE_IDLE;
      break;


    /*******************************************************************************/
    case RFAL_TXRX_STATE_RX_READ_FIFO:

      tmp = rfalFIFOStatusGetNumBytes();
      gRFAL.fifo.bytesTotal += tmp;

      /*******************************************************************************/
      /* Calculate the amount of bytes that still fits in rxBuf                      */
      aux = ((gRFAL.fifo.bytesTotal > rfalConvBitsToBytes(gRFAL.TxRx.ctx.rxBufLen)) ? (rfalConvBitsToBytes(gRFAL.TxRx.ctx.rxBufLen) - gRFAL.fifo.bytesWritten) : tmp);

      /*******************************************************************************/
      /* Retrieve incoming bytes from FIFO to rxBuf, and store already read amount   */
      st25r200ReadFifo(&gRFAL.TxRx.ctx.rxBuf[gRFAL.fifo.bytesWritten], aux);
      gRFAL.fifo.bytesWritten += aux;

      /*******************************************************************************/
      /* If the bytes already read were not the full FIFO WL, dump the remaining     *
       * FIFO so that ST25R391x can continue with reception                          */
      if (aux < tmp) {
        st25r200ReadFifo(NULL, (tmp - aux));
      }

      rfalFIFOStatusClear();
      gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_WAIT_RXE;
      break;


    /*******************************************************************************/
    case RFAL_TXRX_STATE_RX_FAIL:

      /*Check if Observation Mode was enabled and disable it on ST25R391x */
      rfalCheckDisableObsMode();

      /* Clean up Transceive */
      rfalCleanupTransceive();

      /* Error should be assigned by previous state */
      if (gRFAL.TxRx.status == ERR_BUSY) {
        gRFAL.TxRx.status = ERR_SYSTEM;
      }

      gRFAL.TxRx.state = RFAL_TXRX_STATE_IDLE;
      break;

    /*******************************************************************************/
    default:
      gRFAL.TxRx.status = ERR_SYSTEM;
      gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_FAIL;
      break;
  }
}

/*******************************************************************************/
void RfalRfST25R200Class::rfalFIFOStatusUpdate(void)
{
  if (gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] == RFAL_FIFO_STATUS_INVALID) {
    st25r200ReadMultipleRegisters(ST25R200_REG_FIFO_STATUS1, gRFAL.fifo.status, ST25R200_FIFO_STATUS_LEN);
  }
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalFIFOStatusClear(void)
{
  gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] = RFAL_FIFO_STATUS_INVALID;
}


/*******************************************************************************/
uint16_t RfalRfST25R200Class::rfalFIFOStatusGetNumBytes(void)
{
  uint16_t result;

  rfalFIFOStatusUpdate();

  result  = ((((uint16_t)gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] & ST25R200_REG_FIFO_STATUS2_fifo_b8) >> ST25R200_REG_FIFO_STATUS2_fifo_b_shift) << RFAL_BITS_IN_BYTE);
  result |= (((uint16_t)gRFAL.fifo.status[RFAL_FIFO_STATUS_REG1]) & 0x00FFU);
  return result;
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalFIFOStatusIsIncompleteByte(void)
{
  rfalFIFOStatusUpdate();
  return ((gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] & ST25R200_REG_FIFO_STATUS2_fifo_lb_mask) != 0U);
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalFIFOStatusIsMissingPar(void)
{
  rfalFIFOStatusUpdate();
  return ((gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] & ST25R200_REG_FIFO_STATUS2_np_lb) != 0U);
}


/*******************************************************************************/
uint8_t RfalRfST25R200Class::rfalFIFOGetNumIncompleteBits(void)
{
  rfalFIFOStatusUpdate();
  return ((gRFAL.fifo.status[RFAL_FIFO_STATUS_REG2] & ST25R200_REG_FIFO_STATUS2_fifo_lb_mask) >> ST25R200_REG_FIFO_STATUS2_fifo_lb_shift);
}


#if RFAL_FEATURE_NFCA

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalISO14443ATransceiveShortFrame(rfal14443AShortFrameCmd txCmd, uint8_t *rxBuf, uint8_t rxBufLen, uint16_t *rxRcvdLen, uint32_t fwt)
{
  rfalTransceiveContext ctx;
  ReturnCode            ret;
  uint8_t               cmd;

  /* Check if RFAL is properly initialized */
  if ((!st25r200IsTxEnabled()) || (gRFAL.state < RFAL_STATE_MODE_SET) || ((gRFAL.mode != RFAL_MODE_POLL_NFCA) && (gRFAL.mode != RFAL_MODE_POLL_NFCA_T1T))) {
    return ERR_WRONG_STATE;
  }

  /* Check for valid parameters */
  if ((rxBuf == NULL) || (rxRcvdLen == NULL) || (fwt == RFAL_FWT_NONE)) {
    return ERR_PARAM;
  }

  /*******************************************************************************/
  /* Enable collision recognition */
  st25r200SetRegisterBits(ST25R200_REG_PROTOCOL_RX1, ST25R200_REG_PROTOCOL_RX1_antcl);

  cmd = (uint8_t)txCmd;
  ctx.txBuf    = &cmd;
  ctx.txBufLen = RFAL_ISO14443A_SHORTFRAME_LEN;


  /*******************************************************************************/
  /* Prepare for Transceive, Receive only (bypass Tx states) */
  ctx.flags     = ((uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL | (uint32_t)RFAL_TXRX_FLAGS_PAR_TX_NONE | (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_KEEP | (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_MANUAL);
  ctx.rxBuf     = rxBuf;
  ctx.rxBufLen  = rxBufLen;
  ctx.rxRcvdLen = rxRcvdLen;
  ctx.fwt       = fwt;

  EXIT_ON_ERR(ret, rfalStartTransceive(&ctx));

  /*******************************************************************************/
  /* Run Transceive blocking */
  ret = rfalTransceiveRunBlockingTx();
  if (ret == ERR_NONE) {
    ret = rfalTransceiveBlockingRx();
  }

  /* Disable collision detection again */
  st25r200ClrRegisterBits(ST25R200_REG_PROTOCOL_RX1, ST25R200_REG_PROTOCOL_RX1_antcl);
  /*******************************************************************************/

  return ret;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalISO14443ATransceiveAnticollisionFrame(uint8_t *buf, uint8_t *bytesToSend, uint8_t *bitsToSend, uint16_t *rxLength, uint32_t fwt)
{
  ReturnCode ret;

  EXIT_ON_ERR(ret, rfalISO14443AStartTransceiveAnticollisionFrame(buf, bytesToSend, bitsToSend, rxLength, fwt));
  rfalRunBlocking(ret, rfalISO14443AGetTransceiveAnticollisionFrameStatus());

  return ret;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalISO14443AStartTransceiveAnticollisionFrame(uint8_t *buf, uint8_t *bytesToSend, uint8_t *bitsToSend, uint16_t *rxLength, uint32_t fwt)
{
  ReturnCode            ret;
  rfalTransceiveContext ctx;

  /* Check if RFAL is properly initialized */
  if ((gRFAL.state < RFAL_STATE_MODE_SET) || (gRFAL.mode != RFAL_MODE_POLL_NFCA)) {
    return ERR_WRONG_STATE;
  }

  /* Check for valid parameters */
  if ((buf == NULL) || (bytesToSend == NULL) || (bitsToSend == NULL) || (rxLength == NULL)) {
    return ERR_PARAM;
  }

  /*******************************************************************************/
  /* Set specific Analog Config for Anticolission if needed */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_ANTICOL));


  /*******************************************************************************/
  /* Enable collision recognition and place response next to request*/
  st25r200SetRegisterBits(ST25R200_REG_PROTOCOL_RX1, (ST25R200_REG_PROTOCOL_RX1_antcl | ST25R200_REG_PROTOCOL_RX1_rx_nbtx));


  /*******************************************************************************/
  /* Prepare for Transceive                                                      */
  ctx.flags     = ((uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL | (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_KEEP | (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_MANUAL | (uint32_t)RFAL_TXRX_FLAGS_AGC_ON);
  ctx.txBuf     = buf;
  ctx.txBufLen  = (uint16_t)(rfalConvBytesToBits(*bytesToSend) + *bitsToSend);
  ctx.rxBuf     = &buf[*bytesToSend];
  ctx.rxBufLen  = (uint16_t)rfalConvBytesToBits(RFAL_ISO14443A_SDD_RES_LEN);
  ctx.rxRcvdLen = rxLength;
  ctx.fwt       = fwt;

  EXIT_ON_ERR(ret, rfalStartTransceive(&ctx));

  /* Additionally enable bit collision interrupt */
  st25r200GetInterrupt(ST25R200_IRQ_MASK_COL);
  st25r200EnableInterrupts(ST25R200_IRQ_MASK_COL);

  /*******************************************************************************/
  gRFAL.nfcaData.collByte = 0;

  /* Save the collision byte */
  if ((*bitsToSend) > 0U) {
    buf[(*bytesToSend)] <<= (RFAL_BITS_IN_BYTE - (*bitsToSend));
    buf[(*bytesToSend)] >>= (RFAL_BITS_IN_BYTE - (*bitsToSend));
    gRFAL.nfcaData.collByte = buf[(*bytesToSend)];
  }

  gRFAL.nfcaData.buf         = buf;
  gRFAL.nfcaData.bytesToSend = bytesToSend;
  gRFAL.nfcaData.bitsToSend  = bitsToSend;
  gRFAL.nfcaData.rxLength    = rxLength;

  /*******************************************************************************/
  /* Run Transceive Tx */
  return rfalTransceiveRunBlockingTx();
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalISO14443AGetTransceiveAnticollisionFrameStatus(void)
{
  ReturnCode   ret;
  uint8_t      collData;

  EXIT_ON_BUSY(ret, rfalGetTransceiveStatus());

  /*******************************************************************************/
  if ((*gRFAL.nfcaData.bitsToSend) > 0U) {
    gRFAL.nfcaData.buf[(*gRFAL.nfcaData.bytesToSend)] >>= (*gRFAL.nfcaData.bitsToSend);
    gRFAL.nfcaData.buf[(*gRFAL.nfcaData.bytesToSend)] <<= (*gRFAL.nfcaData.bitsToSend);
    gRFAL.nfcaData.buf[(*gRFAL.nfcaData.bytesToSend)] |= gRFAL.nfcaData.collByte;
  }

  if ((ERR_RF_COLLISION == ret)) {
    /* Read out collision register */
    st25r200ReadRegister(ST25R200_REG_COLLISION, &collData);

    (*gRFAL.nfcaData.bytesToSend) = ((collData >> ST25R200_REG_COLLISION_c_byte_shift) & 0x0FU); // 4-bits Byte information
    (*gRFAL.nfcaData.bitsToSend)  = ((collData >> ST25R200_REG_COLLISION_c_bit_shift)  & 0x07U); // 3-bits bit information
  }


  /*******************************************************************************/
  /* Disable Collision interrupt */
  st25r200DisableInterrupts((ST25R200_IRQ_MASK_COL));

  /* Disable collision detection again */
  st25r200ClrRegisterBits(ST25R200_REG_PROTOCOL_RX1, (ST25R200_REG_PROTOCOL_RX1_antcl | ST25R200_REG_PROTOCOL_RX1_rx_nbtx));
  /*******************************************************************************/

  /* Restore common Analog configurations for this mode */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX));
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCA | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX));

  return ret;
}

#endif /* RFAL_FEATURE_NFCA */

#if RFAL_FEATURE_NFCV

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalISO15693TransceiveAnticollisionFrame(uint8_t *txBuf, uint8_t txBufLen, uint8_t *rxBuf, uint8_t rxBufLen, uint16_t *actLen)
{
  ReturnCode            ret;
  rfalTransceiveContext ctx;

  /* Check if RFAL is properly initialized */
  if ((gRFAL.state < RFAL_STATE_MODE_SET) || (gRFAL.mode != RFAL_MODE_POLL_NFCV)) {
    return ERR_WRONG_STATE;
  }

  /*******************************************************************************/
  /* Set specific Analog Config for Anticolission if needed */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | RFAL_ANALOG_CONFIG_BITRATE_COMMON | RFAL_ANALOG_CONFIG_ANTICOL));


  /*******************************************************************************/
  /* Enable anti collision to recognise bit collisions  */
  st25r200SetRegisterBits(ST25R200_REG_PROTOCOL_RX1, ST25R200_REG_PROTOCOL_RX1_antcl);

  /* REMARK: Flag RFAL_TXRX_FLAGS_NFCV_FLAG_MANUAL disregarded */
  /*******************************************************************************/
  /* Prepare for Transceive  */
  ctx.flags     = (RFAL_TXRX_FLAGS_DEFAULT | (uint32_t)RFAL_TXRX_FLAGS_AGC_ON); /* Disable Automatic Gain Control (AGC) for better detection of collision */
  ctx.txBuf     = txBuf;
  ctx.txBufLen  = (uint16_t)rfalConvBytesToBits(txBufLen);
  ctx.rxBuf     = rxBuf;
  ctx.rxBufLen  = (uint16_t)rfalConvBytesToBits(rxBufLen);
  ctx.rxRcvdLen = actLen;
  ctx.fwt       = RFAL_ISO15693_FWT;

  EXIT_ON_ERR(ret, rfalStartTransceive(&ctx));

  /* Additionally enable bit collision interrupt */
  st25r200GetInterrupt(ST25R200_IRQ_MASK_COL);
  st25r200EnableInterrupts(ST25R200_IRQ_MASK_COL);

  /*******************************************************************************/
  /* Run Transceive blocking */
  ret = rfalTransceiveRunBlockingTx();
  if (ret == ERR_NONE) {
    ret = rfalTransceiveBlockingRx();
  }

  /* REMARK: CRC is being returned to keep alignment with ST25R3911/ST25R3916 (due to stream mode limitations) */
  if (ret == ERR_NONE) {
    (*ctx.rxRcvdLen) += (uint16_t)rfalConvBytesToBits(RFAL_CRC_LEN);
  }

  /*******************************************************************************/
  /* Disable Collision interrupt */
  st25r200DisableInterrupts((ST25R200_IRQ_MASK_COL));

  /* Disable collision detection again */
  st25r200ClrRegisterBits(ST25R200_REG_PROTOCOL_RX1, ST25R200_REG_PROTOCOL_RX1_antcl);
  /*******************************************************************************/

  /* Restore common Analog configurations for this mode */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | rfalConvBR2ACBR(gRFAL.txBR) | RFAL_ANALOG_CONFIG_TX));
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_POLL | RFAL_ANALOG_CONFIG_TECH_NFCV | rfalConvBR2ACBR(gRFAL.rxBR) | RFAL_ANALOG_CONFIG_RX));

  return ret;
}

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalISO15693TransceiveEOFAnticollision(uint8_t *rxBuf, uint8_t rxBufLen, uint16_t *actLen)
{
  ReturnCode ret;

  EXIT_ON_ERR(ret, rfalISO15693TransceiveEOF(rxBuf, rxBufLen, actLen));
  (*actLen) = (uint16_t)rfalConvBytesToBits((*actLen));

  return ret;
}

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalISO15693TransceiveEOF(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *actLen)
{
  ReturnCode ret;

  /* Check if RFAL is properly initialized */
  if ((!st25r200IsTxEnabled()) || (gRFAL.state < RFAL_STATE_MODE_SET) || (gRFAL.mode != RFAL_MODE_POLL_NFCV)) {
    return ERR_WRONG_STATE;
  }

  /* Check for valid parameters */
  if ((rxBuf == NULL) || (actLen == NULL)) {
    return ERR_PARAM;
  }


  /*******************************************************************************/
  /* Wait for GT and FDT */
  while (!rfalIsGTExpired())      { /* MISRA 15.6: mandatory brackets */ };
  while (st25r200IsGPTRunning())  { /* MISRA 15.6: mandatory brackets */ };


  gRFAL.tmr.GT = RFAL_TIMING_NONE;

  /*******************************************************************************/
  /* Prepare for Transceive, Receive only (bypass Tx states) */
  gRFAL.TxRx.ctx.flags     = ((uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL);
  gRFAL.TxRx.ctx.rxBuf     = rxBuf;
  gRFAL.TxRx.ctx.rxBufLen  = (uint16_t)rfalConvBytesToBits(rxBufLen);
  gRFAL.TxRx.ctx.rxRcvdLen = actLen;
  gRFAL.TxRx.ctx.fwt       = RFAL_ISO15693_FWT;

  /*******************************************************************************/
  /* ISO15693 EOF frame shall come after Inventory or Write alike command        */
  /* FWT , FDT(Poll), FDT(Listen) must be loaded in the previous transceive      */
  /*******************************************************************************/
  rfalPrepareTransceive();

  /*******************************************************************************/
  /* Enable anti collision to recognise bit collisions  */
  st25r200SetRegisterBits(ST25R200_REG_PROTOCOL_RX1, ST25R200_REG_PROTOCOL_RX1_antcl);

  /* Also enable bit collision interrupt */
  st25r200GetInterrupt(ST25R200_IRQ_MASK_COL);
  st25r200EnableInterrupts(ST25R200_IRQ_MASK_COL);

  /*Check if Observation Mode is enabled and set it on ST25R391x */
  rfalCheckEnableObsModeTx();

  /* Send EOF */
  st25r200ExecuteCommand(ST25R200_CMD_TRANSMIT_EOF);

  /* Wait for TXE */
  if (st25r200WaitForInterruptsTimed(ST25R200_IRQ_MASK_TXE, (uint16_t)MAX(rfalConv1fcToMs(RFAL_ISO15693_FWT), RFAL_ST25R200_SW_TMR_MIN_1MS)) == 0U) {
    ret = ERR_IO;
  } else {
    /*Check if Observation Mode is enabled and set it on ST25R391x */
    rfalCheckEnableObsModeRx();

    /* Jump into a transceive Rx state for reception (bypass Tx states) */
    gRFAL.state       = RFAL_STATE_TXRX;
    gRFAL.TxRx.state  = RFAL_TXRX_STATE_RX_IDLE;
    gRFAL.TxRx.status = ERR_BUSY;

    /* Execute Transceive Rx blocking */
    ret = rfalTransceiveBlockingRx();
  }


  /* Converts received length to bytes */
  (*actLen) = rfalConvBitsToBytes((*actLen));


  /* REMARK: CRC is being returned to keep alignment with ST25R3911/ST25R3916 (due to stream mode limitations) */
  if (ret == ERR_NONE) {
    (*actLen) += RFAL_CRC_LEN;
  }


  /* Disable collision detection again */
  st25r200ClrRegisterBits(ST25R200_REG_PROTOCOL_RX1, ST25R200_REG_PROTOCOL_RX1_antcl);

  /* Disable Collision interrupt */
  st25r200DisableInterrupts((ST25R200_IRQ_MASK_COL));

  return ret;
}

#endif /* RFAL_FEATURE_NFCV */


#if RFAL_FEATURE_NFCF

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalFeliCaPoll(rfalFeliCaPollSlots slots, uint16_t sysCode, uint8_t reqCode, rfalFeliCaPollRes *pollResList, uint8_t pollResListSize, uint8_t *devicesDetected, uint8_t *collisionsDetected)
{
  NO_WARNING(slots);
  NO_WARNING(sysCode);
  NO_WARNING(reqCode);
  NO_WARNING(pollResListSize);

  if (pollResList != NULL) {
    ST_MEMSET(pollResList, 0x00, sizeof(rfalFeliCaPollRes));
  }

  if (devicesDetected != NULL) {
    (*devicesDetected) = 0U;
  }

  if (collisionsDetected != NULL) {
    (*collisionsDetected) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalStartFeliCaPoll(rfalFeliCaPollSlots slots, uint16_t sysCode, uint8_t reqCode, rfalFeliCaPollRes *pollResList, uint8_t pollResListSize, uint8_t *devicesDetected, uint8_t *collisionsDetected)
{
  NO_WARNING(slots);
  NO_WARNING(sysCode);
  NO_WARNING(reqCode);
  NO_WARNING(pollResListSize);

  if (pollResList != NULL) {
    ST_MEMSET(pollResList, 0x00, sizeof(rfalFeliCaPollRes));
  }

  if (devicesDetected != NULL) {
    (*devicesDetected) = 0U;
  }

  if (collisionsDetected != NULL) {
    (*collisionsDetected) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalGetFeliCaPollStatus(void)
{
  return ERR_NOTSUPP;
}

#endif /* RFAL_FEATURE_NFCF */


/*****************************************************************************
 *  Listen Mode                                                              *
 *****************************************************************************/

/*******************************************************************************/
bool RfalRfST25R200Class::rfalIsExtFieldOn(void)
{
  return st25r200IsExtFieldOn();
}

#if RFAL_FEATURE_LISTEN_MODE

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalListenStart(uint32_t lmMask, const rfalLmConfPA *confA, const rfalLmConfPB *confB, const rfalLmConfPF *confF, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rxLen)
{
  NO_WARNING(lmMask);
  NO_WARNING(confA);
  NO_WARNING(confB);
  NO_WARNING(confF);

  if ((rxBuf != NULL) && (rxBufLen > 0U)) {
    ST_MEMSET(rxBuf, 0x00, rfalConvBitsToBytes(rxBufLen));
  }

  if (rxLen != NULL) {
    (*rxLen) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalListenStop(void)
{
  return ERR_NONE;
}

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalListenSleepStart(rfalLmState sleepSt, uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rxLen)
{
  NO_WARNING(sleepSt);
  NO_WARNING(rxBufLen);

  if ((rxBuf != NULL) && (rxBufLen > 0U)) {
    ST_MEMSET(rxBuf, 0x00, rfalConvBitsToBytes(rxBufLen));
  }

  if (rxLen != NULL) {
    (*rxLen) = 0U;
  }

  return ERR_NOTSUPP;
}

/*******************************************************************************/
rfalLmState RfalRfST25R200Class::rfalListenGetState(bool *dataFlag, rfalBitRate *lastBR)
{
  if (dataFlag != NULL) {
    (*dataFlag) = false;
  }

  if (lastBR != NULL) {
    (*lastBR) = RFAL_BR_KEEP;
  }

  return RFAL_LM_STATE_NOT_INIT;
}

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalListenSetState(rfalLmState newSt)
{
  NO_WARNING(newSt);

  return ERR_NOTSUPP;
}

#endif /* RFAL_FEATURE_LISTEN_MODE */


/*******************************************************************************
 *  Wake-Up Mode                                                               *
 *******************************************************************************/

#if RFAL_FEATURE_WAKEUP_MODE

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalWakeUpModeStart(const rfalWakeUpConfig *config)
{
  uint8_t                aux;
  uint8_t                measI;
  uint8_t                measQ;
  uint32_t               irqs;


  /* The Wake-Up procedure is further detailed in Application Note: AN5993 */

  if (config == NULL) {
    gRFAL.wum.cfg.period       = RFAL_WUM_PERIOD_215MS;
    gRFAL.wum.cfg.irqTout      = false;
    gRFAL.wum.cfg.skipCal      = false;
    gRFAL.wum.cfg.skipReCal    = false;
    gRFAL.wum.cfg.delCal       = true;
    gRFAL.wum.cfg.delRef       = true;
    gRFAL.wum.cfg.autoAvg      = true;
    gRFAL.wum.cfg.measFil      = RFAL_WUM_MEAS_FIL_SLOW;
    gRFAL.wum.cfg.measDur      = RFAL_WUM_MEAS_DUR_44_28;

    gRFAL.wum.cfg.indAmp.enabled    = true;
    gRFAL.wum.cfg.cap.enabled    = true;

    gRFAL.wum.cfg.indAmp.delta      = 4U;
    gRFAL.wum.cfg.indAmp.reference  = RFAL_WUM_REFERENCE_AUTO;
    gRFAL.wum.cfg.indAmp.threshold  = ((uint8_t)RFAL_WUM_TRE_ABOVE | (uint8_t)RFAL_WUM_TRE_BELOW);
    gRFAL.wum.cfg.indAmp.aaWeight   = RFAL_WUM_AA_WEIGHT_32;
    gRFAL.wum.cfg.indAmp.aaInclMeas = true;

    gRFAL.wum.cfg.cap.delta      = 4U;
    gRFAL.wum.cfg.cap.reference  = RFAL_WUM_REFERENCE_AUTO;
    gRFAL.wum.cfg.cap.threshold  = ((uint8_t)RFAL_WUM_TRE_ABOVE | (uint8_t)RFAL_WUM_TRE_BELOW);
    gRFAL.wum.cfg.cap.aaWeight   = RFAL_WUM_AA_WEIGHT_32;
    gRFAL.wum.cfg.cap.aaInclMeas = true;
  } else {
    gRFAL.wum.cfg = *config;
  }

  /* Check for valid configuration */
  if (((gRFAL.wum.cfg.indAmp.enabled == false) && (gRFAL.wum.cfg.cap.enabled == false))   ||               /* Running wake-up requires one of the modes being enabled      */
      ((gRFAL.wum.cfg.indAmp.enabled == true)  && (gRFAL.wum.cfg.indAmp.threshold == 0U))    ||               /* If none of the threshold bits is set the WU will not executed */
      ((gRFAL.wum.cfg.cap.enabled == true)  && (gRFAL.wum.cfg.cap.threshold == 0U))
     ) {
    return ERR_PARAM;
  }


  irqs  = ST25R200_IRQ_MASK_NONE;
  measI = 0U;
  measQ = 0U;

  /* Disable Tx, Rx */
  st25r200TxRxOff();

  /* Set Analog configurations for Wake-up On event */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_WAKEUP_ON));


  /*******************************************************************************/
  /* Prepare Wake-Up Timer Control Register */
  aux = (uint8_t)(((uint8_t)gRFAL.wum.cfg.period & 0x0FU) << ST25R200_REG_WAKEUP_CONF1_wut_shift);

  if (gRFAL.wum.cfg.irqTout) {
    aux  |= ST25R200_REG_WAKEUP_CONF1_wuti;
    irqs |= ST25R200_IRQ_MASK_WUT;
  }

  st25r200WriteRegister(ST25R200_REG_WAKEUP_CONF1, aux);


  /* Prepare Wake-Up  Control Register 2 */
  aux  = 0U;
  aux |= (uint8_t)(gRFAL.wum.cfg.skipReCal                           ? ST25R200_REG_WAKEUP_CONF2_skip_recal : 0x00U);
  aux |= (uint8_t)(gRFAL.wum.cfg.skipCal                             ? ST25R200_REG_WAKEUP_CONF2_skip_cal   : 0x00U);
  aux |= (uint8_t)(gRFAL.wum.cfg.delCal                              ? 0x00U : ST25R200_REG_WAKEUP_CONF2_skip_twcal);
  aux |= (uint8_t)(gRFAL.wum.cfg.delRef                              ? 0x00U : ST25R200_REG_WAKEUP_CONF2_skip_twref);
  aux |= (uint8_t)(gRFAL.wum.cfg.autoAvg                             ? ST25R200_REG_WAKEUP_CONF2_iq_uaaref  : 0x00U);
  aux |= (uint8_t)((gRFAL.wum.cfg.measFil == RFAL_WUM_MEAS_FIL_FAST) ? ST25R200_REG_WAKEUP_CONF2_td_mf      : 0x00U);
  aux |= (uint8_t)((uint8_t)gRFAL.wum.cfg.measDur & ST25R200_REG_WAKEUP_CONF2_td_mt_mask);

  st25r200WriteRegister(ST25R200_REG_WAKEUP_CONF2, aux);


  /* Check if a manual reference is to be obtained */
  if ((!gRFAL.wum.cfg.autoAvg)                                                                  &&
      (((gRFAL.wum.cfg.indAmp.reference == RFAL_WUM_REFERENCE_AUTO) && (gRFAL.wum.cfg.indAmp.enabled))  ||
       ((gRFAL.wum.cfg.cap.reference == RFAL_WUM_REFERENCE_AUTO) && (gRFAL.wum.cfg.cap.enabled)))) {
    /* Disable calibration automatics, perform manual calibration before reference measurement */
    st25r200SetRegisterBits(ST25R200_REG_WAKEUP_CONF2, (ST25R200_REG_WAKEUP_CONF2_skip_cal | ST25R200_REG_WAKEUP_CONF2_skip_recal));

    /* Perform Manual Calibration and enter PD mode*/
    st25r200CalibrateWU(NULL, NULL);
    st25r200ClrRegisterBits(ST25R200_REG_OPERATION, ST25R200_REG_OPERATION_en);

    delay(RFAL_PD_SETTLE);
    st25r200MeasureWU(&measI, &measQ);
  }


  /*******************************************************************************/
  /* Check if I-Channel is to be checked */
  if (gRFAL.wum.cfg.indAmp.enabled) {
    st25r200ChangeRegisterBits(ST25R200_REG_WU_I_DELTA, ST25R200_REG_WU_I_DELTA_i_diff_mask, gRFAL.wum.cfg.indAmp.delta);

    aux  = 0U;
    aux |= (uint8_t)(gRFAL.wum.cfg.indAmp.aaInclMeas ? ST25R200_REG_WU_I_CONF_i_iirqm : 0x00U);
    aux |= (uint8_t)(((uint8_t)gRFAL.wum.cfg.indAmp.aaWeight << ST25R200_REG_WU_I_CONF_i_aaw_shift) & ST25R200_REG_WU_I_CONF_i_aaw_mask);
    aux |= (uint8_t)(gRFAL.wum.cfg.indAmp.threshold & ST25R200_REG_WU_I_CONF_i_tdi_en_mask);
    st25r200WriteRegister(ST25R200_REG_WU_I_CONF, aux);

    if (!gRFAL.wum.cfg.autoAvg) {
      /* Set reference manually */
      st25r200WriteRegister(ST25R200_REG_WU_I_REF, ((gRFAL.wum.cfg.indAmp.reference == RFAL_WUM_REFERENCE_AUTO) ? measI : gRFAL.wum.cfg.indAmp.reference));
    }

    irqs |= ST25R200_IRQ_MASK_WUI;
  } else {
    st25r200ClrRegisterBits(ST25R200_REG_WU_I_CONF, ST25R200_REG_WU_I_CONF_i_tdi_en_mask);
  }

  /*******************************************************************************/
  /* Check if Q-Channel is to be checked */
  if (gRFAL.wum.cfg.cap.enabled) {
    st25r200ChangeRegisterBits(ST25R200_REG_WU_Q_DELTA, ST25R200_REG_WU_Q_DELTA_q_diff_mask, gRFAL.wum.cfg.cap.delta);

    aux = 0U;
    aux |= (uint8_t)(gRFAL.wum.cfg.cap.aaInclMeas ? ST25R200_REG_WU_Q_CONF_q_iirqm : 0x00U);
    aux |= (uint8_t)(((uint8_t)gRFAL.wum.cfg.cap.aaWeight << ST25R200_REG_WU_Q_CONF_q_aaw_shift) & ST25R200_REG_WU_Q_CONF_q_aaw_mask);
    aux |= (uint8_t)(gRFAL.wum.cfg.cap.threshold & ST25R200_REG_WU_Q_CONF_q_tdi_en_mask);
    st25r200WriteRegister(ST25R200_REG_WU_Q_CONF, aux);

    if (!gRFAL.wum.cfg.autoAvg) {
      /* Set reference manually */
      st25r200WriteRegister(ST25R200_REG_WU_Q_REF, ((gRFAL.wum.cfg.cap.reference == RFAL_WUM_REFERENCE_AUTO) ? measQ : gRFAL.wum.cfg.cap.reference));
    }

    irqs |= ST25R200_IRQ_MASK_WUQ;
  } else {
    st25r200ClrRegisterBits(ST25R200_REG_WU_Q_CONF, ST25R200_REG_WU_Q_CONF_q_tdi_en_mask);
  }

  /* Disable and clear all interrupts except Wake-Up IRQs */
  st25r200DisableInterrupts(ST25R200_IRQ_MASK_ALL);
  st25r200GetInterrupt(irqs);
  st25r200EnableInterrupts(irqs);

  /* Disable Oscilattor, Tx, Rx and Regulators */
  st25r200ClrRegisterBits(ST25R200_REG_OPERATION, (ST25R200_REG_OPERATION_tx_en | ST25R200_REG_OPERATION_rx_en | ST25R200_REG_OPERATION_am_en | ST25R200_REG_OPERATION_en));

  /* Clear WU info struct */
  ST_MEMSET(&gRFAL.wum.info, 0x00, sizeof(gRFAL.wum.info));

  gRFAL.wum.state = RFAL_WUM_STATE_ENABLED;
  gRFAL.state     = RFAL_STATE_WUM;

  /* Enable Low Power Wake-Up Mode */
  st25r200SetRegisterBits(ST25R200_REG_OPERATION, ST25R200_REG_OPERATION_wu_en);

  return ERR_NONE;
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalWakeUpModeHasWoke(void)
{
  return (gRFAL.wum.state >= RFAL_WUM_STATE_ENABLED_WOKE);
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalWakeUpModeIsEnabled(void)
{
  return ((gRFAL.state == RFAL_STATE_WUM) && (gRFAL.wum.state >= RFAL_WUM_STATE_ENABLED));
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalWakeUpModeGetInfo(bool force, rfalWakeUpInfo *info)
{
  /* Check if WU mode is running */
  if ((gRFAL.state != RFAL_STATE_WUM) || (gRFAL.wum.state < RFAL_WUM_STATE_ENABLED)) {
    return ERR_WRONG_STATE;
  }

  /* Check for valid parameters */
  if (info == NULL) {
    return ERR_PARAM;
  }

  /* Clear info structure */
  ST_MEMSET(info, 0x00, sizeof(rfalWakeUpInfo));

  /* Update general information */
  info->irqWut          = gRFAL.wum.info.irqWut;
  gRFAL.wum.info.irqWut = false;

  /* WUT IRQ is signaled when WUT expires. Delay slightly for the actual measurement to be performed */
  if (info->irqWut) {
    delay(1);
  }

  /* Retrieve values if there was an WUT, WUI or WUQ event (or forced) */
  if (force || (info->irqWut) || (gRFAL.wum.info.irqWui) || (gRFAL.wum.info.irqWuq)) {
    /* Update status information */
    st25r200ReadRegister(ST25R200_REG_DISPLAY4, &info->status);
    info->status &= (ST25R200_REG_DISPLAY4_q_tdi_mask | ST25R200_REG_DISPLAY4_i_tdi_mask);

    if (gRFAL.wum.cfg.indAmp.enabled) {
      st25r200ReadRegister(ST25R200_REG_WU_I_ADC, &info->indAmp.lastMeas);
      st25r200ReadRegister(ST25R200_REG_WU_I_CAL, &info->indAmp.calib);
      st25r200ReadRegister(ST25R200_REG_WU_I_REF, &info->indAmp.reference);

      /* Update IRQ information and clear flag upon retrieving */
      info->indAmp.irqWu         = gRFAL.wum.info.irqWui;
      gRFAL.wum.info.irqWui = false;
    }

    if (gRFAL.wum.cfg.cap.enabled) {
      st25r200ReadRegister(ST25R200_REG_WU_Q_ADC, &info->cap.lastMeas);
      st25r200ReadRegister(ST25R200_REG_WU_Q_CAL, &info->cap.calib);
      st25r200ReadRegister(ST25R200_REG_WU_Q_REF, &info->cap.reference);

      /* Update IRQ information and clear flag upon retrieving */
      info->cap.irqWu         = gRFAL.wum.info.irqWuq;
      gRFAL.wum.info.irqWuq = false;
    }
  }

  return ERR_NONE;
}


/*******************************************************************************/
void RfalRfST25R200Class::rfalRunWakeUpModeWorker(void)
{
  uint32_t irqs;
  uint8_t  aux;

  if (gRFAL.state != RFAL_STATE_WUM) {
    return;
  }

  switch (gRFAL.wum.state) {
    case RFAL_WUM_STATE_ENABLED:
    case RFAL_WUM_STATE_ENABLED_WOKE:

      irqs = st25r200GetInterrupt((ST25R200_IRQ_MASK_WUT | ST25R200_IRQ_MASK_WUI | ST25R200_IRQ_MASK_WUQ));
      if (irqs == ST25R200_IRQ_MASK_NONE) {
        break;  /* No interrupt to process */
      }

      /*******************************************************************************/
      /* Check and mark which measurement(s) cause interrupt */
      if ((irqs & ST25R200_IRQ_MASK_WUI) != 0U) {
        gRFAL.wum.info.irqWui = true;
        st25r200ReadRegister(ST25R200_REG_WU_I_ADC, &aux);
        gRFAL.wum.state = RFAL_WUM_STATE_ENABLED_WOKE;
      }

      if ((irqs & ST25R200_IRQ_MASK_WUQ) != 0U) {
        gRFAL.wum.info.irqWuq = true;
        st25r200ReadRegister(ST25R200_REG_WU_Q_ADC, &aux);
        gRFAL.wum.state = RFAL_WUM_STATE_ENABLED_WOKE;
      }

      if ((irqs & ST25R200_IRQ_MASK_WUT) != 0U) {
        gRFAL.wum.info.irqWut = true;
      }

      break;

    default:
      /* MISRA 16.4: no empty default statement (a comment being enough) */
      break;
  }
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalWakeUpModeStop(void)
{
  if (gRFAL.wum.state == RFAL_WUM_STATE_NOT_INIT) {
    return ERR_WRONG_STATE;
  }

  gRFAL.wum.state = RFAL_WUM_STATE_NOT_INIT;

  /* Disable Wake-Up Mode */
  st25r200ClrRegisterBits(ST25R200_REG_OPERATION, ST25R200_REG_OPERATION_wu_en);
  st25r200DisableInterrupts((ST25R200_IRQ_MASK_WUT | ST25R200_IRQ_MASK_WUQ | ST25R200_IRQ_MASK_WUI));

  /* Re-Enable the Oscillator and Regulators */
  st25r200OscOn();

  /* Stop any ongoing activity */
  st25r200ExecuteCommand(ST25R200_CMD_STOP);

  /* Set Analog configurations for Wake-up Off event */
  rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_WAKEUP_OFF));

  return ERR_NONE;
}

#endif /* RFAL_FEATURE_WAKEUP_MODE */


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalWlcPWptMonitorStart(const rfalWakeUpConfig *config)
{
  NO_WARNING(config);

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalWlcPWptMonitorStop(void)
{
  return ERR_NOTSUPP;
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalWlcPWptIsFodDetected(void)
{
  return false;
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalWlcPWptIsStopDetected(void)
{
  return false;
}


/*******************************************************************************
 *  Low-Power Mode                                                             *
 *******************************************************************************/

#if RFAL_FEATURE_LOWPOWER_MODE

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalLowPowerModeStart(rfalLpMode mode)
{
  /* Check if RFAL is not initialized */
  if (gRFAL.state < RFAL_STATE_INIT) {
    return ERR_WRONG_STATE;
  }

  if (mode == RFAL_LP_MODE_HR) {
#ifndef ST25R_RESET_PIN
    return ERR_DISABLED;
#else
    digitalWrite(ST25R_RESET_PIN, HIGH);
#endif /* ST25R_RESET_PIN */
  } else {
    /* Stop any ongoing activity and set the device in low power by disabling oscillator, transmitter, receiver and AM regulator */
    st25r200ExecuteCommand(ST25R200_CMD_STOP);
    st25r200ClrRegisterBits(ST25R200_REG_OPERATION, (ST25R200_REG_OPERATION_en    | ST25R200_REG_OPERATION_rx_en |
                                                     ST25R200_REG_OPERATION_wu_en | ST25R200_REG_OPERATION_tx_en | ST25R200_REG_OPERATION_am_en));

    rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_LOWPOWER_ON));

  }

  gRFAL.state         = RFAL_STATE_IDLE;
  gRFAL.lpm.isRunning = true;
  gRFAL.lpm.mode      = mode;

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalLowPowerModeStop(void)
{
  ReturnCode ret;

  /* Check if RFAL is on right state */
  if (!gRFAL.lpm.isRunning) {
    return ERR_WRONG_STATE;
  }

#ifdef ST25R_RESET_PIN
  if (gRFAL.lpm.mode == RFAL_LP_MODE_HR) {
    rfalInitialize();
  } else
#endif /* ST25R_RESET_PIN */
  {
    /* Re-enable device */
    EXIT_ON_ERR(ret, st25r200OscOn());

    rfalSetAnalogConfig((RFAL_ANALOG_CONFIG_TECH_CHIP | RFAL_ANALOG_CONFIG_CHIP_LOWPOWER_OFF));
  }

  gRFAL.state         = RFAL_STATE_INIT;
  gRFAL.lpm.isRunning = false;
  return ERR_NONE;
}

#endif /* RFAL_FEATURE_LOWPOWER_MODE */

/*******************************************************************************
 *  RF Chip                                                                    *
 *******************************************************************************/

/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipWriteReg(uint16_t reg, const uint8_t *values, uint8_t len)
{
  if (!st25r200IsRegValid((uint8_t)reg)) {
    return ERR_PARAM;
  }

  return st25r200WriteMultipleRegisters((uint8_t)reg, values, len);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipReadReg(uint16_t reg, uint8_t *values, uint8_t len)
{
  if (!st25r200IsRegValid((uint8_t)reg)) {
    return ERR_PARAM;
  }

  return st25r200ReadMultipleRegisters((uint8_t)reg, values, len);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipExecCmd(uint16_t cmd)
{
  if (!st25r200IsCmdValid((uint8_t)cmd)) {
    return ERR_PARAM;
  }

  return st25r200ExecuteCommand((uint8_t) cmd);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipWriteTestReg(uint16_t reg, uint8_t value)
{
  return st25r200WriteTestRegister((uint8_t)reg, value);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipReadTestReg(uint16_t reg, uint8_t *value)
{
  return st25r200ReadTestRegister((uint8_t)reg, value);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipChangeRegBits(uint16_t reg, uint8_t valueMask, uint8_t value)
{
  if (!st25r200IsRegValid((uint8_t)reg)) {
    return ERR_PARAM;
  }

  return st25r200ChangeRegisterBits((uint8_t)reg, valueMask, value);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipChangeTestRegBits(uint16_t reg, uint8_t valueMask, uint8_t value)
{
  return st25r200ChangeTestRegisterBits((uint8_t)reg, valueMask, value);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipSetRFO(uint8_t rfo)
{
  return st25r200ChangeRegisterBits(ST25R200_REG_TX_DRIVER, ST25R200_REG_TX_DRIVER_d_res_mask, rfo);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipGetRFO(uint8_t *result)
{
  ReturnCode ret;

  ret = st25r200ReadRegister(ST25R200_REG_TX_DRIVER, result);

  if (result != NULL) {
    (*result) = ((*result) & ST25R200_REG_TX_DRIVER_d_res_mask);
  }

  return ret;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipSetLMMod(uint8_t mod, uint8_t unmod)
{
  NO_WARNING(mod);
  NO_WARNING(unmod);

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipGetLMMod(uint8_t *mod, uint8_t *unmod)
{
  if (mod != NULL) {
    (*mod) = 0U;
  }

  if (unmod != NULL) {
    (*unmod) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipMeasureAmplitude(uint8_t *result)
{
  if (result != NULL) {
    (*result) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipMeasurePhase(uint8_t *result)
{
  if (result != NULL) {
    (*result) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipMeasureCapacitance(uint8_t *result)
{
  if (result != NULL) {
    (*result) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipMeasurePowerSupply(uint8_t param, uint8_t *result)
{
  NO_WARNING(param);

  if (result != NULL) {
    (*result) = 0U;
  }

  return ERR_NOTSUPP;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipMeasureIQ(int8_t *resI, int8_t *resQ)
{
  st25r200ClearCalibration();
  return st25r200MeasureIQ(resI, resQ);
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipMeasureCombinedIQ(uint8_t *result)
{
  ReturnCode err;
  uint8_t    reg;

  /* Apply|Use same sensitivity setting between measurements*/
  st25r200ReadRegister(ST25R200_REG_RX_ANA2, &reg);
  st25r200WriteRegister(ST25R200_REG_RX_ANA2, ((reg & ~ST25R200_REG_RX_ANA2_afe_gain_td_mask) | ST25R200_REG_RX_ANA2_afe_gain_td2 | ST25R200_REG_RX_ANA2_afe_gain_td1));

  err = st25r200MeasureCombinedIQ(result);

  /* Restore previous sensitivity */
  st25r200WriteRegister(ST25R200_REG_RX_ANA2, reg);

  return err;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalChipSetAntennaMode(bool single, bool rfiox)
{
  return st25r200SetAntennaMode(single, rfiox);
}


