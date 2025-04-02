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
 *  \brief ST25R200 Interrupt handling
 *
 */

/*
******************************************************************************
* INCLUDES
******************************************************************************
*/
#include "rfal_rfst25r200.h"
#include "st25r200_interrupt.h"
#include "st25r200_com.h"
#include "st25r200.h"
#include "st_errno.h"
#include "nfc_utils.h"

/*
 ******************************************************************************
 * LOCAL DATA TYPES
 ******************************************************************************
 */


/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/

/*! Length of the interrupt registers       */
#define ST25R200_INT_REGS_LEN          ( (ST25R200_REG_IRQ3 - ST25R200_REG_IRQ1) + 1U )


/*
******************************************************************************
* GLOBAL VARIABLES
******************************************************************************
*/


/*
******************************************************************************
* GLOBAL FUNCTIONS
******************************************************************************
*/

void RfalRfST25R200Class::st25r200InitInterrupts(void)
{
  st25r200interrupt.callback     = NULL;
  st25r200interrupt.prevCallback = NULL;
  st25r200interrupt.status       = ST25R200_IRQ_MASK_NONE;
  st25r200interrupt.mask         = ST25R200_IRQ_MASK_NONE;
}


/*******************************************************************************/
void RfalRfST25R200Class::st25r200Isr(void)
{
  st25r200CheckForReceivedInterrupts();

  /* Check if callback is set and run it */
  if (NULL != st25r200interrupt.callback) {
    st25r200interrupt.callback();
  }
}


/*******************************************************************************/
void RfalRfST25R200Class::st25r200CheckForReceivedInterrupts(void)
{
  uint8_t  iregs[ST25R200_INT_REGS_LEN];
  uint32_t irqStatus;

#ifdef ST25R_POLL_IRQ
  /* Exit immediately in case of no IRQ */
  if (digitalRead(int_pin) == LOW) {
    return;
  }
#endif /* ST25R_POLL_IRQ */

  /* Initialize iregs */
  irqStatus = ST25R200_IRQ_MASK_NONE;
  ST_MEMSET(iregs, (int32_t)(ST25R200_IRQ_MASK_ALL & 0xFFU), ST25R200_INT_REGS_LEN);

  /* In case the IRQ is Edge (not Level) triggered read IRQs until done */
  while (digitalRead(int_pin) == HIGH) {
    st25r200ReadMultipleRegisters(ST25R200_REG_IRQ1, iregs, ST25R200_INT_REGS_LEN);

    irqStatus |= (uint32_t)iregs[0];
    irqStatus |= (uint32_t)iregs[1] << 8;
    irqStatus |= (uint32_t)iregs[2] << 16;
  }

  /* Forward all interrupts, even masked ones to application */
  st25r200interrupt.status |= irqStatus;

  /* Send an IRQ event to LED handling */
  // st25r200ledEvtIrq( st25r200interrupt.status );
}


/*******************************************************************************/
void RfalRfST25R200Class::st25r200ModifyInterrupts(uint32_t clr_mask, uint32_t set_mask)
{
  uint8_t  i;
  uint32_t old_mask;
  uint32_t new_mask;

  old_mask = st25r200interrupt.mask;
  new_mask = ((~old_mask & set_mask) | (old_mask & clr_mask));
  st25r200interrupt.mask &= ~clr_mask;
  st25r200interrupt.mask |= set_mask;

  for (i = 0; i < ST25R200_INT_REGS_LEN; i++) {
    if (((new_mask >> (8U * i)) & 0xFFU) == 0U) {
      continue;
    }

    st25r200WriteRegister(ST25R200_REG_IRQ_MASK1 + i, (uint8_t)((st25r200interrupt.mask >> (8U * i)) & 0xFFU));
  }
  return;
}


/*******************************************************************************/
uint32_t RfalRfST25R200Class::st25r200WaitForInterruptsTimed(uint32_t mask, uint16_t tmo)
{
  uint32_t tmrDelay;
  uint32_t status;

  tmrDelay = timerCalculateTimer(tmo);

  /* Run until specific interrupt has happen or the timer has expired */
  do {
#ifdef ST25R_POLL_IRQ
    st25r200CheckForReceivedInterrupts();
#endif /* ST25R_POLL_IRQ */

    status = (st25r200interrupt.status & mask);
  } while (((!timerIsExpired(tmrDelay)) || (tmo == 0U)) && (status == 0U));


  status = st25r200interrupt.status & mask;


  st25r200interrupt.status &= ~status;

  return status;
}


/*******************************************************************************/
uint32_t RfalRfST25R200Class::st25r200GetInterrupt(uint32_t mask)
{
  uint32_t irqs;

  irqs = (st25r200interrupt.status & mask);
  if (irqs != ST25R200_IRQ_MASK_NONE) {
    st25r200interrupt.status &= ~irqs;
  }

  return irqs;
}


/*******************************************************************************/
void RfalRfST25R200Class::st25r200ClearAndEnableInterrupts(uint32_t mask)
{
  st25r200GetInterrupt(mask);
  st25r200EnableInterrupts(mask);
}


/*******************************************************************************/
void RfalRfST25R200Class::st25r200EnableInterrupts(uint32_t mask)
{
  st25r200ModifyInterrupts(mask, 0);
}


/*******************************************************************************/
void RfalRfST25R200Class::st25r200DisableInterrupts(uint32_t mask)
{
  st25r200ModifyInterrupts(0, mask);
}

/*******************************************************************************/
void RfalRfST25R200Class::st25r200ClearInterrupts(void)
{
  uint8_t iregs[ST25R200_INT_REGS_LEN];

  st25r200ReadMultipleRegisters(ST25R200_REG_IRQ1, iregs, ST25R200_INT_REGS_LEN);

  st25r200interrupt.status = ST25R200_IRQ_MASK_NONE;
  return;
}

/*******************************************************************************/
void RfalRfST25R200Class::st25r200IRQCallbackSet(void (*cb)(void))
{
  st25r200interrupt.prevCallback = st25r200interrupt.callback;
  st25r200interrupt.callback     = cb;
}

/*******************************************************************************/
void RfalRfST25R200Class::st25r200IRQCallbackRestore(void)
{
  st25r200interrupt.callback     = st25r200interrupt.prevCallback;
  st25r200interrupt.prevCallback = NULL;
}