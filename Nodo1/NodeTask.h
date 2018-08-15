#ifndef TASKS_NODETASK_H_
#define TASKS_NODETASK_H_

#include <ti/drivers/rf/RF.h>

#ifdef FEATURE_BLE_ADV
#include "ble_adv/BleAdv.h"
#endif

/* Initializes the Node Task and creates all TI-RTOS objects */
void NodeTask_init(void);

#ifdef FEATURE_BLE_ADV
/* display advertisement stats */
void NodeTask_advStatsCB(BleAdv_Stats stats);
extern void rfSwitchCallback(RF_Handle h, RF_ClientEvent event, void* arg);
#endif

#endif /* TASKS_NODETASK_H_ */
