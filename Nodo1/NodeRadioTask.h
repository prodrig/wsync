#ifndef TASKS_NODERADIOTASKTASK_H_
#define TASKS_NODERADIOTASKTASK_H_

#include "stdint.h"

#define NODE_ACTIVITY_LED Board_PIN_LED0

enum NodeRadioOperationStatus {
    NodeRadioStatus_Success,
    NodeRadioStatus_Failed,
    NodeRadioStatus_FailedNotConnected,
};

/* Initializes the NodeRadioTask and creates all TI-RTOS objects */
void NodeRadioTask_init(void);

/* Sends an ADC value to the concentrator */
enum NodeRadioOperationStatus NodeRadioTask_sendAdcData(uint16_t data);

/* Get node address, return 0 if node address has not been set */
uint8_t nodeRadioTask_getNodeAddr(void);
void nodeRadioTask_addNodeAddr(void);
void nodeRadioTask_subNodeAddr(void);

#endif /* TASKS_NODERADIOTASKTASK_H_ */
