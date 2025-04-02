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
 *  \brief Functions to manage and set analog settings.
 *
 */

/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include "rfal_rfst25r200.h"
#include "rfal_rfst25r200_analogConfig.h"
#include "st_errno.h"
#include "nfc_utils.h"
#include "rfal_rfst25r200_analogConfigTbl.h"


/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */


#define RFAL_TEST_REG         0x0080U      /*!< Test Register indicator  */

/*
 ******************************************************************************
 * LOCAL TABLES
 ******************************************************************************
 */

/*
 ******************************************************************************
 * LOCAL FUNCTION PROTOTYPES
 ******************************************************************************
 */

/*
 ******************************************************************************
 * GLOBAL VARIABLE DEFINITIONS
 ******************************************************************************
 */

/*
 ******************************************************************************
 * GLOBAL FUNCTIONS
 ******************************************************************************
 */

/*******************************************************************************/
void RfalRfST25R200Class::rfalAnalogConfigInitialize(void)
{
  /* Use default Analog configuration settings in Flash by default. */

  /* Check whether the Default Analog settings are to be used or custom ones */
  gRfalAnalogConfigMgmt.currentAnalogConfigTbl = (const uint8_t *)&rfalAnalogConfigDefaultSettings;
  gRfalAnalogConfigMgmt.configTblSize          = sizeof(rfalAnalogConfigDefaultSettings);


  gRfalAnalogConfigMgmt.ready = true;
}


/*******************************************************************************/
bool RfalRfST25R200Class::rfalAnalogConfigIsReady(void)
{
  return gRfalAnalogConfigMgmt.ready;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalAnalogConfigListWriteRaw(const uint8_t *configTbl, uint16_t configTblSize)
{
  // If Analog Configuration Update is to be disabled
  NO_WARNING(configTbl);
  NO_WARNING(configTblSize);
  return ERR_REQUEST;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalAnalogConfigListWrite(uint8_t more, const rfalAnalogConfig *config)
{
  // If Analog Configuration Update is to be disabled
  NO_WARNING(config);
  NO_WARNING(more);
  return ERR_DISABLED;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalAnalogConfigListReadRaw(uint8_t *tblBuf, uint16_t tblBufLen, uint16_t *configTblSize)
{
  /* Check if the the current table will fit into the given buffer */
  if (tblBufLen < gRfalAnalogConfigMgmt.configTblSize) {
    return ERR_NOMEM;
  }

  /* Check for invalid parameters */
  if ((configTblSize == NULL) || (tblBuf == NULL)) {
    return ERR_PARAM;
  }

  /* Copy the whole Table to the given buffer */
  if (gRfalAnalogConfigMgmt.configTblSize > 0U) {                    /* MISRA 21.18 */
    ST_MEMCPY(tblBuf, gRfalAnalogConfigMgmt.currentAnalogConfigTbl, gRfalAnalogConfigMgmt.configTblSize);
  }
  *configTblSize = gRfalAnalogConfigMgmt.configTblSize;

  return ERR_NONE;
}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalAnalogConfigListRead(rfalAnalogConfigOffset *configOffset, uint8_t *more, rfalAnalogConfig *config, rfalAnalogConfigNum numConfig)
{
  uint16_t configSize;
  rfalAnalogConfigOffset offset = *configOffset;
  rfalAnalogConfigNum numConfigSet;

  /* Check if the number of register-mask-value settings for the respective Configuration ID will fit into the buffer passed in. */
  if (gRfalAnalogConfigMgmt.currentAnalogConfigTbl[offset + sizeof(rfalAnalogConfigId)] > numConfig) {
    return ERR_NOMEM;
  }

  /* Get the number of Configuration set */
  numConfigSet = gRfalAnalogConfigMgmt.currentAnalogConfigTbl[offset + sizeof(rfalAnalogConfigId)];

  /* Pass Configuration Register-Mask-Value sets */
  configSize = (sizeof(rfalAnalogConfigId) + sizeof(rfalAnalogConfigNum) + (uint16_t)(numConfigSet * sizeof(rfalAnalogConfigRegAddrMaskVal)));
  ST_MEMCPY((uint8_t *) config
            , &gRfalAnalogConfigMgmt.currentAnalogConfigTbl[offset]
            , configSize
           );
  *configOffset = offset + configSize;

  /* Check if it is the last Analog Configuration in the Table.*/
  *more = (uint8_t)((*configOffset >= gRfalAnalogConfigMgmt.configTblSize) ? RFAL_ANALOG_CONFIG_UPDATE_LAST
                    : RFAL_ANALOG_CONFIG_UPDATE_MORE);

  return ERR_NONE;

}


/*******************************************************************************/
ReturnCode RfalRfST25R200Class::rfalSetAnalogConfig(rfalAnalogConfigId configId)
{
  rfalAnalogConfigOffset configOffset = 0;
  rfalAnalogConfigNum numConfigSet;
  rfalAnalogConfigRegAddrMaskVal *configTbl;
  ReturnCode retCode = ERR_NONE;
  rfalAnalogConfigNum i;

  if (true != gRfalAnalogConfigMgmt.ready) {
    return ERR_REQUEST;
  }

  /* Search LUT for the specific Configuration ID */
  while (true) {
    numConfigSet = rfalAnalogConfigSearch(configId, &configOffset);
    if (RFAL_ANALOG_CONFIG_LUT_NOT_FOUND == numConfigSet) {
      break;
    }

    configTbl = (rfalAnalogConfigRegAddrMaskVal *)((uint32_t)gRfalAnalogConfigMgmt.currentAnalogConfigTbl + (uint32_t)configOffset);
    /* Increment the offset to the next index to search from */
    configOffset += (uint16_t)(numConfigSet * sizeof(rfalAnalogConfigRegAddrMaskVal));

    if ((gRfalAnalogConfigMgmt.configTblSize + 1U) < configOffset) {
      /* Error check make sure that the we do not access outside the configuration Table Size */
      return ERR_NOMEM;
    }

    for (i = 0; i < numConfigSet; i++) {
      if ((GETU16(configTbl[i].addr) & RFAL_TEST_REG) != 0U) {
        EXIT_ON_ERR(retCode, rfalChipChangeTestRegBits((GETU16(configTbl[i].addr) & ~RFAL_TEST_REG), configTbl[i].mask, configTbl[i].val));
      } else {
        EXIT_ON_ERR(retCode, rfalChipChangeRegBits(GETU16(configTbl[i].addr), configTbl[i].mask, configTbl[i].val));
      }
    }

  } /* while(found Analog Config Id) */

  return retCode;
}


/*******************************************************************************/
uint16_t RfalRfST25R200Class::rfalAnalogConfigGenModeID(rfalMode md, rfalBitRate br, uint16_t dir)
{
  uint16_t id;

  /* Assign Poll/Listen Mode */
  id = ((md >= RFAL_MODE_LISTEN_NFCA) ? RFAL_ANALOG_CONFIG_LISTEN : RFAL_ANALOG_CONFIG_POLL);

  /* Assign Technology */
  switch (md) {
    case RFAL_MODE_POLL_NFCA:
    case RFAL_MODE_POLL_NFCA_T1T:
    case RFAL_MODE_LISTEN_NFCA:
      id |= RFAL_ANALOG_CONFIG_TECH_NFCA;
      break;

    case RFAL_MODE_POLL_NFCB:
    case RFAL_MODE_POLL_B_PRIME:
    case RFAL_MODE_POLL_B_CTS:
    case RFAL_MODE_LISTEN_NFCB:
      id |= RFAL_ANALOG_CONFIG_TECH_NFCB;
      break;

    case RFAL_MODE_POLL_NFCF:
    case RFAL_MODE_LISTEN_NFCF:
      id |= RFAL_ANALOG_CONFIG_TECH_NFCF;
      break;

    case RFAL_MODE_POLL_NFCV:
    case RFAL_MODE_POLL_PICOPASS:
      id |= RFAL_ANALOG_CONFIG_TECH_NFCV;
      break;

    case RFAL_MODE_POLL_ACTIVE_P2P:
    case RFAL_MODE_LISTEN_ACTIVE_P2P:
      id |= RFAL_ANALOG_CONFIG_TECH_AP2P;
      break;

    default:
      id = RFAL_ANALOG_CONFIG_TECH_CHIP;
      break;
  }

  /* Assign Bitrate */
  id |= (((((uint16_t)(br) >= (uint16_t)RFAL_BR_52p97) ? (uint16_t)(br) : ((uint16_t)(br) + 1U)) << RFAL_ANALOG_CONFIG_BITRATE_SHIFT) & RFAL_ANALOG_CONFIG_BITRATE_MASK);

  /* Assign Direction */
  id |= ((dir << RFAL_ANALOG_CONFIG_DIRECTION_SHIFT) & RFAL_ANALOG_CONFIG_DIRECTION_MASK);

  return id;
}

/*
 ******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************
 */

/*!
 *****************************************************************************
 * \brief  Search the Analog Configuration LUT for a specific Configuration ID.
 *
 * Search the Analog Configuration LUT for the Configuration ID.
 *
 * \param[in]  configId: Configuration ID to search for.
 * \param[in]  configOffset: Configuration Offset in Table
 *
 * \return number of Configuration Sets
 * \return #RFAL_ANALOG_CONFIG_LUT_NOT_FOUND in case Configuration ID is not found.
 *****************************************************************************
 */
rfalAnalogConfigNum RfalRfST25R200Class::rfalAnalogConfigSearch(rfalAnalogConfigId configId, uint16_t *configOffset)
{
  rfalAnalogConfigId foundConfigId;
  rfalAnalogConfigId configIdMaskVal;
  const uint8_t      *configTbl;
  const uint8_t      *currentConfigTbl;
  uint16_t i;

  currentConfigTbl = gRfalAnalogConfigMgmt.currentAnalogConfigTbl;
  configIdMaskVal  = ((RFAL_ANALOG_CONFIG_POLL_LISTEN_MODE_MASK | RFAL_ANALOG_CONFIG_BITRATE_MASK)
                      | ((RFAL_ANALOG_CONFIG_TECH_CHIP == RFAL_ANALOG_CONFIG_ID_GET_TECH(configId)) ? (RFAL_ANALOG_CONFIG_TECH_MASK | RFAL_ANALOG_CONFIG_CHIP_SPECIFIC_MASK) : configId)
                      | ((RFAL_ANALOG_CONFIG_NO_DIRECTION == RFAL_ANALOG_CONFIG_ID_GET_DIRECTION(configId)) ? RFAL_ANALOG_CONFIG_DIRECTION_MASK : configId)
                     );


  /* When specific ConfigIDs are to be used, override search mask */
  if ((RFAL_ANALOG_CONFIG_ID_GET_DIRECTION(configId) == RFAL_ANALOG_CONFIG_DPO) || (RFAL_ANALOG_CONFIG_ID_GET_DIRECTION(configId) == RFAL_ANALOG_CONFIG_DLMA)) {
    configIdMaskVal = (RFAL_ANALOG_CONFIG_POLL_LISTEN_MODE_MASK | RFAL_ANALOG_CONFIG_TECH_MASK | RFAL_ANALOG_CONFIG_BITRATE_MASK | RFAL_ANALOG_CONFIG_DIRECTION_MASK);
  }

  i = (*configOffset);
  while (i < gRfalAnalogConfigMgmt.configTblSize) {
    configTbl = &currentConfigTbl[i];
    foundConfigId = GETU16(configTbl);
    if (configId == (foundConfigId & configIdMaskVal)) {
      *configOffset = (uint16_t)(i + sizeof(rfalAnalogConfigId) + sizeof(rfalAnalogConfigNum));
      return configTbl[sizeof(rfalAnalogConfigId)];
    }

    /* If Config Id does not match, increment to next Configuration Id */
    i += (uint16_t)(sizeof(rfalAnalogConfigId) + sizeof(rfalAnalogConfigNum)
                    + (configTbl[sizeof(rfalAnalogConfigId)] * sizeof(rfalAnalogConfigRegAddrMaskVal))
                   );
  } /* while */

  return RFAL_ANALOG_CONFIG_LUT_NOT_FOUND;
}
