/***** Includes *****/
/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
/* BIOS Header files */ 
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Clock.h>
/* TI-RTOS Header files */ 
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/drivers/rf/RF.h>
#include <ti/drivers/PIN.h>
/* Board Header files */
#include "Board.h"
/* Standard C Libraries */
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
/* EasyLink API Header files */ 
#include "easylink/EasyLink.h"
/* Application Header files */ 
#include "RadioProtocol.h"
#include "NodeRadioTask.h"
#include "NodeTask.h"
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/aon_batmon.h)
#include DeviceFamily_constructPath(driverlib/trng.h)

#ifdef FEATURE_BLE_ADV
#include "ble_adv/BleAdv.h"
#endif

/***** Defines *****/
#define NODERADIO_TASK_STACK_SIZE 1024
#define NODERADIO_TASK_PRIORITY   3
#define READY_TIME  5 /* Time to wait for ready address */
#define INTERVAL_PACKET_TIME    30 /* Time between packets */
#define MAX_NODES   255
/***** Events *****/
#define RADIO_EVENT_ALL                 0xFFFFFFFF
#define RADIO_EVENT_SEND_ADC_DATA       (uint32_t)(1 << 0)
#define RADIO_EVENT_DATA_ACK_RECEIVED   (uint32_t)(1 << 1)
#define RADIO_EVENT_ACK_TIMEOUT         (uint32_t)(1 << 2)
#define RADIO_EVENT_SEND_FAIL           (uint32_t)(1 << 3)
#define RADIO_EVENT_OTHER_NODE_DATA_SENDED (uint32_t)(1 << 4)
#ifdef FEATURE_BLE_ADV
#define NODE_EVENT_UBLE                 (uint32_t)(1 << 5)
#endif
#define NODERADIO_MAX_RETRIES 2 /* Max number of retries to resend packet */
#define NODERADIO_ACK_TIMEOUT_TIME_MS (1000) /* Normal timeout */
#define NODERADIO_TIMEOUT_OTHER_NODE_TIME_MS (5000) /* Timeout for other node */

/***** Type declarations *****/
struct RadioOperation {
    EasyLink_TxPacket easyLinkTxPacket;
    uint8_t retriesDone;
    uint8_t maxNumberOfRetries;
    uint32_t ackTimeoutMs;
    enum NodeRadioOperationStatus result;
};

/***** Variable declarations *****/
static Task_Params nodeRadioTaskParams;
Task_Struct nodeRadioTask;
static uint8_t nodeRadioTaskStack[NODERADIO_TASK_STACK_SIZE];
Semaphore_Struct radioAccessSem;
static Semaphore_Handle radioAccessSemHandle;
Event_Struct radioOperationEvent;
static Event_Handle radioOperationEventHandle;
Semaphore_Struct radioResultSem;
static Semaphore_Handle radioResultSemHandle;
static struct RadioOperation currentRadioOperation;
static uint16_t adcData;
static uint8_t nodeAddress = 1; /* Node address */
static struct DualModeSensorPacket dmSensorPacket;
static volatile time_t t;
static int ACKReceived = 0; /* Flag for state wait mode */
static uint8_t dstAddress = 0; /* Destination address */
static time_t nodeCurrentTime; /* Current time of node */
struct tm* time_info; /* Time to struct */
static uint32_t prevTicks; /* Previous tick count used to calculate uptime */
static uint8_t sourceAddress[MAX_NODES]; /* Array to save the node address */

/* Pin driver handle */
extern PIN_Handle ledPinHandle;

/***** Prototypes *****/
static void nodeRadioTaskFunction(UArg arg0, UArg arg1);
static void returnRadioOperationStatus(enum NodeRadioOperationStatus status);
static void sendDmPacket(struct DualModeSensorPacket sensorPacket, uint8_t maxNumberOfRetries, uint32_t ackTimeoutMs);
static void resendPacket(void);
static void rxDoneCallback(EasyLink_RxPacket * rxPacket, EasyLink_Status status);
static void rxOtherNode(EasyLink_RxPacket * rxPacket, EasyLink_Status status);
static void sendACKOtherNode(EasyLink_RxPacket * rxPacket, EasyLink_Status status);
static struct AckPacket ackPacket;

#ifdef FEATURE_BLE_ADV
static void bleAdv_eventProxyCB(void);
static void bleAdv_updateTlmCB(uint16_t *pVbatt, uint16_t *pTemp, uint32_t *pTime100MiliSec);
static void bleAdv_updateMsButtonCB(uint8_t *pButton);
#endif

/***** Function definitions *****/
void NodeRadioTask_init(void) {

    /* Create semaphore used for exclusive radio access */
    Semaphore_Params semParam;
    Semaphore_Params_init(&semParam);
    Semaphore_construct(&radioAccessSem, 1, &semParam);
    radioAccessSemHandle = Semaphore_handle(&radioAccessSem);

    /* Create semaphore used for callers to wait for result */
    Semaphore_construct(&radioResultSem, 0, &semParam);
    radioResultSemHandle = Semaphore_handle(&radioResultSem);

    /* Create event used internally for state changes */
    Event_Params eventParam;
    Event_Params_init(&eventParam);
    Event_construct(&radioOperationEvent, &eventParam);
    radioOperationEventHandle = Event_handle(&radioOperationEvent);

    /* Create the radio protocol task */
    Task_Params_init(&nodeRadioTaskParams);
    nodeRadioTaskParams.stackSize = NODERADIO_TASK_STACK_SIZE;
    nodeRadioTaskParams.priority = NODERADIO_TASK_PRIORITY;
    nodeRadioTaskParams.stack = &nodeRadioTaskStack;
    Task_construct(&nodeRadioTask, nodeRadioTaskFunction, &nodeRadioTaskParams, NULL);
}

uint8_t nodeRadioTask_getNodeAddr(void)
{
    return nodeAddress;
}

/* Up the node address when push button 2 */
void nodeRadioTask_addNodeAddr(void)
{
    t = time(0);
    if (nodeAddress < 255) nodeAddress++;
}
/* Down the node address when push button 1 */
void nodeRadioTask_subNodeAddr(void)
{
    t = time(0);
    if (nodeAddress > 1) nodeAddress--;
}

static void nodeRadioTaskFunction(UArg arg0, UArg arg1)
{
    /* Wait until you stop pressing the buttons */
    t = time(0);
    while(time(0) < t+READY_TIME) sleep(1);
#ifdef FEATURE_BLE_ADV
    BleAdv_Params_t bleAdv_Params;
    /* Set mulitclient mode for EasyLink */
    EasyLink_setCtrl(EasyLink_Ctrl_MultiClient_Mode, 1);

#endif

    EasyLink_Params easyLink_params;
    EasyLink_Params_init(&easyLink_params);

    easyLink_params.ui32ModType = RADIO_EASYLINK_MODULATION;

    /* Initialize EasyLink */
    if(EasyLink_init(&easyLink_params) != EasyLink_Status_Success){
        System_abort("EasyLink_init failed");
    }

    /* If you wich to use a frequency other than the default use
     * the below API
     * EasyLink_setFrequency(868000000);
     */

    /* Set the filter to the selected address */
    if (EasyLink_enableRxAddrFilter(&nodeAddress, 1, 1) != EasyLink_Status_Success)
    {
        System_abort("EasyLink_enableRxAddrFilter failed");
    }

    /* Setup ADC sensor packet */
    dmSensorPacket.header.sourceAddress = nodeAddress;
    dmSensorPacket.header.packetType = RADIO_PACKET_TYPE_DM_SENSOR_PACKET;

    /* Initialise previous Tick count used to calculate uptime for the TLM beacon */
    prevTicks = Clock_getTicks();

#ifdef FEATURE_BLE_ADV
    /* Initialize the Simple Beacon module wit default params */
    BleAdv_Params_init(&bleAdv_Params);
    bleAdv_Params.pfnPostEvtProxyCB = bleAdv_eventProxyCB;
    bleAdv_Params.pfnUpdateTlmCB = bleAdv_updateTlmCB;
    bleAdv_Params.pfnUpdateMsButtonCB = bleAdv_updateMsButtonCB;
    bleAdv_Params.pfnAdvStatsCB = NodeTask_advStatsCB;
    BleAdv_init(&bleAdv_Params);

    /* initialize BLE advertisements to default to MS */
    BleAdv_setAdvertiserType(BleAdv_AdertiserMs);
#endif

    /* Enter main task loop */
    while (1)
    {
        /* Wait for an event */
        uint32_t events = Event_pend(radioOperationEventHandle, 0, RADIO_EVENT_ALL, BIOS_WAIT_FOREVER);

        /* If we should send ADC data */
        if (events & RADIO_EVENT_SEND_ADC_DATA)
        {
            /* If ACK is received synchronize the current time and wait for time secs is 30 or 00 (30 seconds) */

            time(&nodeCurrentTime);
            time_info = localtime(&nodeCurrentTime);
            if (ACKReceived) {
                while (time_info->tm_sec != 30 && time_info->tm_sec != 0)
                {
                    time(&nodeCurrentTime);
                    time_info = localtime(&nodeCurrentTime);
                    sleep(1);
                }
            }
            //dmSensorPacket.header.sourceAddress = nodeAddress;
            uint32_t currentTicks;

            currentTicks = Clock_getTicks();
            //check for wrap around
            if (currentTicks > prevTicks)
            {
                //calculate time since last reading in 0.1s units
                dmSensorPacket.time100MiliSec += ((currentTicks - prevTicks) * Clock_tickPeriod) / 100000;
            }
            else
            {
                //calculate time since last reading in 0.1s units
                dmSensorPacket.time100MiliSec += ((prevTicks - currentTicks) * Clock_tickPeriod) / 100000;
            }
            prevTicks = currentTicks;

            dmSensorPacket.batt = AONBatMonBatteryVoltageGet();
            dmSensorPacket.adcValue = adcData;
            dmSensorPacket.button = !PIN_getInputValue(Board_PIN_BUTTON0);

            sendDmPacket(dmSensorPacket, NODERADIO_MAX_RETRIES, NODERADIO_ACK_TIMEOUT_TIME_MS);

        }

        /* If we get an ACK from the concentrator */
        if (events & RADIO_EVENT_DATA_ACK_RECEIVED)
        {
            EasyLink_setCtrl(EasyLink_Ctrl_AsyncRx_TimeOut, EasyLink_ms_To_RadioTime(NODERADIO_TIMEOUT_OTHER_NODE_TIME_MS)); /* Set timeout for possible other node */
            if (EasyLink_receiveAsync(rxOtherNode, 0) != EasyLink_Status_Success)
                {
                    System_abort("EasyLink_receiveAsync failed");
                }
        }
        /* If receive packet of other node and transmitted to concentrator */
        if (events & RADIO_EVENT_OTHER_NODE_DATA_SENDED)
        {
            EasyLink_setCtrl(EasyLink_Ctrl_AsyncRx_TimeOut, EasyLink_ms_To_RadioTime(NODERADIO_ACK_TIMEOUT_TIME_MS)); /* Set normal timeout */
            if (EasyLink_receiveAsync(sendACKOtherNode, 0) != EasyLink_Status_Success)
                            {
                                System_abort("EasyLink_receiveAsync failed");
                            }
        }

        /* If we get an ACK timeout */
        if (events & RADIO_EVENT_ACK_TIMEOUT)
        {

            /* If we haven't resent it the maximum number of times yet, then resend packet */
            if (currentRadioOperation.retriesDone < currentRadioOperation.maxNumberOfRetries)
            {
                resendPacket();
            }
            else
            {
                /* Else return send fail */
                Event_post(radioOperationEventHandle, RADIO_EVENT_SEND_FAIL);
            }
        }

        /* If send fail */
        if (events & RADIO_EVENT_SEND_FAIL)
        {
            ACKReceived = 0;
            if (dstAddress == 255) dstAddress = 0;
            else dstAddress++;
            returnRadioOperationStatus(NodeRadioStatus_Failed);
        }

#ifdef FEATURE_BLE_ADV
        if (events & NODE_EVENT_UBLE)
        {
            uble_processMsg();
        }
#endif
    }
}

enum NodeRadioOperationStatus NodeRadioTask_sendAdcData(uint16_t data)
{
    enum NodeRadioOperationStatus status;

    /* Get radio access semaphore */
    Semaphore_pend(radioAccessSemHandle, BIOS_WAIT_FOREVER);

    /* Save data to send */
    adcData = data;

    /* Raise RADIO_EVENT_SEND_ADC_DATA event */
    Event_post(radioOperationEventHandle, RADIO_EVENT_SEND_ADC_DATA);

    /* Wait for result */
    Semaphore_pend(radioResultSemHandle, BIOS_WAIT_FOREVER);

    /* Get result */
    status = currentRadioOperation.result;

    /* Return radio access semaphore */
    Semaphore_post(radioAccessSemHandle);

    return status;
}

static void returnRadioOperationStatus(enum NodeRadioOperationStatus result)
{
    /* Save result */
    currentRadioOperation.result = result;

    /* Post result semaphore */
    Semaphore_post(radioResultSemHandle);
}

static void sendDmPacket(struct DualModeSensorPacket sensorPacket, uint8_t maxNumberOfRetries, uint32_t ackTimeoutMs)
{
    /* Set destination address in EasyLink API */
    currentRadioOperation.easyLinkTxPacket.dstAddr[0] = dstAddress;

    /* Set humidity, temperature, third involved and clock */
    dmSensorPacket.humidity1 = 1;
    dmSensorPacket.humidity2 = 2;
    dmSensorPacket.humidity3 = 3;
    dmSensorPacket.humidity4 = 4;
    dmSensorPacket.humidity5 = 5;
    dmSensorPacket.humidity6 = 6;
    dmSensorPacket.temperature = 22;
    dmSensorPacket.header.thirdAddress = nodeAddress;
    dmSensorPacket.header.clock = *((uint32_t*)&nodeCurrentTime);
    /* Copy ADC packet to payload
     * Note that the EasyLink API will implicitly both add the length byte and the destination address byte. */
    currentRadioOperation.easyLinkTxPacket.payload[0] = dmSensorPacket.header.sourceAddress;
    currentRadioOperation.easyLinkTxPacket.payload[1] = dmSensorPacket.header.packetType;
    currentRadioOperation.easyLinkTxPacket.payload[2] = dmSensorPacket.header.thirdAddress;
    currentRadioOperation.easyLinkTxPacket.payload[3] = (dmSensorPacket.header.clock & 0xFF000000) >> 24;
    currentRadioOperation.easyLinkTxPacket.payload[4] = (dmSensorPacket.header.clock & 0x00FF0000) >> 16;
    currentRadioOperation.easyLinkTxPacket.payload[5] = (dmSensorPacket.header.clock & 0xFF00) >> 8;
    currentRadioOperation.easyLinkTxPacket.payload[6] = (dmSensorPacket.header.clock & 0xFF);
    currentRadioOperation.easyLinkTxPacket.payload[7] = (dmSensorPacket.adcValue & 0xFF00) >> 8;
    currentRadioOperation.easyLinkTxPacket.payload[8] = (dmSensorPacket.adcValue & 0xFF);
    currentRadioOperation.easyLinkTxPacket.payload[9] = (dmSensorPacket.batt & 0xFF00) >> 8;
    currentRadioOperation.easyLinkTxPacket.payload[10] = (dmSensorPacket.batt & 0xFF);
    currentRadioOperation.easyLinkTxPacket.payload[11] = (dmSensorPacket.time100MiliSec & 0xFF000000) >> 24;
    currentRadioOperation.easyLinkTxPacket.payload[12] = (dmSensorPacket.time100MiliSec & 0x00FF0000) >> 16;
    currentRadioOperation.easyLinkTxPacket.payload[13] = (dmSensorPacket.time100MiliSec & 0xFF00) >> 8;
    currentRadioOperation.easyLinkTxPacket.payload[14] = (dmSensorPacket.time100MiliSec & 0xFF);
    currentRadioOperation.easyLinkTxPacket.payload[15] = dmSensorPacket.button;
    currentRadioOperation.easyLinkTxPacket.payload[16] = dmSensorPacket.humidity1;
    currentRadioOperation.easyLinkTxPacket.payload[17] = dmSensorPacket.humidity2;
    currentRadioOperation.easyLinkTxPacket.payload[18] = dmSensorPacket.humidity3;
    currentRadioOperation.easyLinkTxPacket.payload[19] = dmSensorPacket.humidity4;
    currentRadioOperation.easyLinkTxPacket.payload[20] = dmSensorPacket.humidity5;
    currentRadioOperation.easyLinkTxPacket.payload[21] = dmSensorPacket.humidity6;
    currentRadioOperation.easyLinkTxPacket.payload[22] = (dmSensorPacket.temperature & 0xFF000000) >> 24;
    currentRadioOperation.easyLinkTxPacket.payload[23] = (dmSensorPacket.temperature & 0x00FF0000) >> 16;
    currentRadioOperation.easyLinkTxPacket.payload[24] = (dmSensorPacket.temperature & 0xFF00) >> 8;
    currentRadioOperation.easyLinkTxPacket.payload[25] = (dmSensorPacket.temperature & 0xFF);
    currentRadioOperation.easyLinkTxPacket.len = sizeof(struct DualModeSensorPacket);
    /* Setup retries */
    currentRadioOperation.maxNumberOfRetries = maxNumberOfRetries;
    currentRadioOperation.ackTimeoutMs = ackTimeoutMs;
    currentRadioOperation.retriesDone = 0;
    EasyLink_setCtrl(EasyLink_Ctrl_AsyncRx_TimeOut, EasyLink_ms_To_RadioTime(ackTimeoutMs)); /* Set normal timeout */

    /* Send packet  */
    if (EasyLink_transmit(&currentRadioOperation.easyLinkTxPacket) != EasyLink_Status_Success)
    {
        System_abort("EasyLink_transmit failed");
    }
#if defined(Board_DIO30_SWPWR)
    /* this was a blocking call, so Tx is now complete. Turn off the RF switch power */
    PIN_setOutputValue(blePinHandle, Board_DIO30_SWPWR, 0);
#endif

    /* Enter RX */
    if (EasyLink_receiveAsync(rxDoneCallback, 0) != EasyLink_Status_Success)
    {
        System_abort("EasyLink_receiveAsync failed");
    }
}

static void resendPacket(void)
{
    /* Send packet  */
    if (EasyLink_transmit(&currentRadioOperation.easyLinkTxPacket) != EasyLink_Status_Success)
    {
        System_abort("EasyLink_transmit failed");
    }
#if defined(Board_DIO30_SWPWR)
    /* this was a blocking call, so Tx is now complete. Turn off the RF switch power */
    PIN_setOutputValue(blePinHandle, Board_DIO30_SWPWR, 0);
#endif

    /* Enter RX and wait for ACK with timeout */
    if (EasyLink_receiveAsync(rxDoneCallback, 0) != EasyLink_Status_Success)
    {
        System_abort("EasyLink_receiveAsync failed");
    }

    /* Increase retries by one */
    currentRadioOperation.retriesDone++;
}

#ifdef FEATURE_BLE_ADV
/*********************************************************************
* @fn      bleAdv_eventProxyCB
*
* @brief   Post an event to the application so that a Micro BLE Stack internal
*          event is processed by Micro BLE Stack later in the application
*          task's context.
*
* @param   None
*
* @return  None
*/
static void bleAdv_eventProxyCB(void)
{
    /* Post event */
    Event_post(radioOperationEventHandle, NODE_EVENT_UBLE);
}

/*********************************************************************
* @fn      bleAdv_updateTlmCB

* @brief Callback to update the TLM data
*
* @param pvBatt Battery level
* @param pTemp Current temperature
* @param pTime100MiliSec time since boot in 100ms units
*
* @return  None
*/
static void bleAdv_updateTlmCB(uint16_t *pvBatt, uint16_t *pTemp, uint32_t *pTime100MiliSec)
{
    uint32_t currentTicks = Clock_getTicks();

    //check for wrap around
    if (currentTicks > prevTicks)
    {
        //calculate time since last reading in 0.1s units
        *pTime100MiliSec += ((currentTicks - prevTicks) * Clock_tickPeriod) / 100000;
    }
    else
    {
        //calculate time since last reading in 0.1s units
        *pTime100MiliSec += ((prevTicks - currentTicks) * Clock_tickPeriod) / 100000;
    }
    prevTicks = currentTicks;

    *pvBatt = AONBatMonBatteryVoltageGet();
    // Battery voltage (bit 10:8 - integer, but 7:0 fraction)
    *pvBatt = (*pvBatt * 125) >> 5; // convert V to mV

    *pTemp = adcData;
}

/*********************************************************************
* @fn      bleAdv_updateMsButtonCB
*
* @brief Callback to update the MS button data
*
* @param pButton Button state to be added to MS beacon Frame
*
* @return  None
*/
static void bleAdv_updateMsButtonCB(uint8_t *pButton)
{
    *pButton = !PIN_getInputValue(Board_PIN_BUTTON0);
}
#endif

/* Reception ACK of concentrator function */

static void rxDoneCallback(EasyLink_RxPacket * rxPacket, EasyLink_Status status)
{
    struct PacketHeader* packetHeader;

#if defined(Board_DIO30_SWPWR)
    /* Rx is now complete. Turn off the RF switch power */
    PIN_setOutputValue(blePinHandle, Board_DIO30_SWPWR, 0);
#endif

    /* If this callback is called because of a packet received */
    if (status == EasyLink_Status_Success)
    {
        /* Check the payload header */
        packetHeader = (struct PacketHeader*)rxPacket->payload;

        /* Check if this is an ACK packet */
        if (packetHeader->packetType == RADIO_PACKET_TYPE_ACK_PACKET)
        {
            /* Synchronize clock from concentrator */
            time_t concentratorClock = (rxPacket->payload[3] << 24) | (rxPacket->payload[4] << 16) | (rxPacket->payload[5] << 8) | rxPacket->payload[6];
            nodeCurrentTime += (int)difftime(concentratorClock, nodeCurrentTime);
            /* Signal ACK packet received */
            Event_post(radioOperationEventHandle, RADIO_EVENT_DATA_ACK_RECEIVED);
        }
        else
        {
            /* If receive packet of other node before the ACK of concentrator */
            rxOtherNode(rxPacket, status);
        }
    }
    /* did the Rx timeout */
    else if(status == EasyLink_Status_Rx_Timeout)
    {
        /* Post a RADIO_EVENT_ACK_TIMEOUT event */
        Event_post(radioOperationEventHandle, RADIO_EVENT_ACK_TIMEOUT);
    }
    else
    {
        /* The Ack receiption may have been corrupted causing an error.
         * Treat this as a timeout
         */
        Event_post(radioOperationEventHandle, RADIO_EVENT_ACK_TIMEOUT);
    }
}
/* If receive packet of other node, send it to concentrator, else, continue normal operation sensor */
static void rxOtherNode(EasyLink_RxPacket * rxPacket, EasyLink_Status status)
{
    if (status == EasyLink_Status_Success)
    {
        if (rxPacket->len > 7) /* Avoid the ACK packets */
        {
            sourceAddress[(int) rxPacket->payload[2]] = rxPacket->payload[0]; /* Save the source address */
            /* Prepare the txpacket for transmit to concentrator or previous node */
            EasyLink_TxPacket txpacket;
            int i;
            txpacket.dstAddr[0] = dstAddress;
            txpacket.absTime = rxPacket->absTime;
            for (i = 1; i < rxPacket->len; i++)
                txpacket.payload[i] = rxPacket->payload[i]; /* Copy the rxpacket to txpacket */
            txpacket.payload[0] = nodeAddress; /* Set the source address (himself) */
            txpacket.payload[2] = rxPacket->payload[0]; /* Set the third involved (other node address) */
            txpacket.len = rxPacket->len;
            if (EasyLink_transmit(&txpacket) != EasyLink_Status_Success)
            {
                System_abort("EasyLink_transmit failed");
            }
            Event_post(radioOperationEventHandle,
                       RADIO_EVENT_OTHER_NODE_DATA_SENDED); /* Active event */
        }
    }
    /* If timeout because no receive other node packet */
    else
    {
        ACKReceived = 1;
        returnRadioOperationStatus(NodeRadioStatus_Success);
    }
}
/* Send the ack received from concentrator to other node */
static void sendACKOtherNode(EasyLink_RxPacket * rxPacket, EasyLink_Status status)
{
    struct PacketHeader* packhead;

    if (status == EasyLink_Status_Success)
        {
        packhead = (struct PacketHeader*)rxPacket->payload;

                /* Check if this is an ACK packet */
                if (packhead->packetType == RADIO_PACKET_TYPE_ACK_PACKET)
                {
                    EasyLink_TxPacket txPacket;
                                    txPacket.dstAddr[0] = sourceAddress[(int)packhead->thirdAddress]; /* Set address saved previously */
                                    ackPacket.header.sourceAddress = nodeAddress;
                                    ackPacket.header.packetType = packhead->packetType;
                                    ackPacket.header.thirdAddress = packhead->thirdAddress;
                                    ackPacket.header.clock = nodeCurrentTime; /* Send the current time node to other node for synchronization */
                                    txPacket.payload[0] = ackPacket.header.sourceAddress;
                                        txPacket.payload[1] = ackPacket.header.packetType;
                                        txPacket.payload[2] = ackPacket.header.thirdAddress;
                                        txPacket.payload[3] = (ackPacket.header.clock & 0xFF000000) >> 24;
                                        txPacket.payload[4] = (ackPacket.header.clock & 0x00FF0000) >> 16;
                                        txPacket.payload[5] = (ackPacket.header.clock & 0xFF00) >> 8;
                                        txPacket.payload[6] = (ackPacket.header.clock & 0xFF);
                                        txPacket.len = sizeof(ackPacket);
                                    if (EasyLink_transmit(&txPacket) != EasyLink_Status_Success)
                                        {
                                        System_abort("EasyLink_transmit failed");
                                        }
                                    Event_post(radioOperationEventHandle, RADIO_EVENT_DATA_ACK_RECEIVED);
                }
        }
    /* If timeout, continue normally */
    else
    {
        Event_post(radioOperationEventHandle, RADIO_EVENT_DATA_ACK_RECEIVED);
    }
}
