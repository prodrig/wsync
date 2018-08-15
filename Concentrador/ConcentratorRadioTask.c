/***** Includes *****/
/* Standard C Libraries */
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* XDCtools Header files */ 
#include <xdc/std.h>
#include <xdc/runtime/System.h>

#include "ConcentratorRadioTask.h"

/* BIOS Header files */ 
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Event.h>

/* Drivers */
#include <ti/drivers/rf/RF.h>
#include <ti/drivers/PIN.h>

/* Board Header files */
#include "Board.h"

/* EasyLink API Header files */ 
#include "easylink/EasyLink.h"

/* Application Header files */ 
#include "RadioProtocol.h"


/***** Defines *****/
#define CONCENTRATORRADIO_TASK_STACK_SIZE 1024
#define CONCENTRATORRADIO_TASK_PRIORITY   3
#define RADIO_EVENT_ALL                  0xFFFFFFFF
#define RADIO_EVENT_VALID_PACKET_RECEIVED      (uint32_t)(1 << 0)
#define RADIO_EVENT_INVALID_PACKET_RECEIVED (uint32_t)(1 << 1)
#define READY_TIME  5
#define CONCENTRATORRADIO_MAX_RETRIES 2
#define NORERADIO_ACK_TIMEOUT_TIME_MS (1000)
#define MAX_NODES   255

#define CONCENTRATOR_ACTIVITY_LED Board_PIN_LED0

/***** Type declarations *****/



/***** Variable declarations *****/
static Task_Params concentratorRadioTaskParams;
Task_Struct concentratorRadioTask;
static uint8_t concentratorRadioTaskStack[CONCENTRATORRADIO_TASK_STACK_SIZE];
Event_Struct radioOperationEvent;
static Event_Handle radioOperationEventHandle;
static volatile int number_nodes = 2; /* Number of nodes */
static volatile time_t t;

static ConcentratorRadio_PacketReceivedCallback packetReceivedCallback;
static union ConcentratorPacket latestRxPacket;
static EasyLink_TxPacket txPacket;
static struct AckPacket ackPacket;
static uint8_t concentratorAddress;
static int8_t latestRssi;
static time_t concentratorCurrentTime;
struct tm* time_info;
static uint8_t nodes[MAX_NODES]; /* Array for save directions of nodes */
static int counterNodes = 0; /* Counter for count the nodes has send a packet */


/***** Prototypes *****/
static void concentratorRadioTaskFunction(UArg arg0, UArg arg1);
static void rxDoneCallback(EasyLink_RxPacket * rxPacket, EasyLink_Status status);
static void notifyPacketReceived(union ConcentratorPacket* latestRxPacket);
static void sendAck(uint8_t latestSourceAddress);

/* Pin driver handle */
static PIN_Handle ledPinHandle;
static PIN_State ledPinState;

/* Configure LED Pin */
PIN_Config ledPinTable[] = {
        CONCENTRATOR_ACTIVITY_LED | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

#include <ti/display/Display.h>

Display_Handle display;


/***** Function definitions *****/
void ConcentratorRadioTask_init(void) {
    display = Display_open(Display_Type_UART, NULL);
    /* Open LED pins */
    ledPinHandle = PIN_open(&ledPinState, ledPinTable);
	if (!ledPinHandle)
	{
        System_abort("Error initializing board 3.3V domain pins\n");
    }

    /* Create event used internally for state changes */
    Event_Params eventParam;
    Event_Params_init(&eventParam);
    Event_construct(&radioOperationEvent, &eventParam);
    radioOperationEventHandle = Event_handle(&radioOperationEvent);

    /* Create the concentrator radio protocol task */
    Task_Params_init(&concentratorRadioTaskParams);
    concentratorRadioTaskParams.stackSize = CONCENTRATORRADIO_TASK_STACK_SIZE;
    concentratorRadioTaskParams.priority = CONCENTRATORRADIO_TASK_PRIORITY;
    concentratorRadioTaskParams.stack = &concentratorRadioTaskStack;
    Task_construct(&concentratorRadioTask, concentratorRadioTaskFunction, &concentratorRadioTaskParams, NULL);
}

void ConcentratorRadioTask_registerPacketReceivedCallback(ConcentratorRadio_PacketReceivedCallback callback) {
    packetReceivedCallback = callback;
}
/* Up the number of nodes when press the button 2 */
void ConcentratorRadioTask_nodeAddr()
{
    t = time(0);
    if (number_nodes < MAX_NODES) number_nodes++;
}
/* Down the number of nodes when press the button 1 */
void ConcentratorRadioTask_nodeSub()
{
    t = time(0);
    if (number_nodes > 1) number_nodes--;
}

static void concentratorRadioTaskFunction(UArg arg0, UArg arg1)
{
    /* Wait until you stop pressing the buttons */
    t = time(0);
    while(time(0) < t+READY_TIME) sleep(1);
    int i;
    for (i = 0; i < MAX_NODES; i++) nodes[i] = 0; /* Initialize the array of node address */
    /* Initialize EasyLink */
	EasyLink_Params easyLink_params;
    EasyLink_Params_init(&easyLink_params);
	
	easyLink_params.ui32ModType = RADIO_EASYLINK_MODULATION;
	
	if(EasyLink_init(&easyLink_params) != EasyLink_Status_Success){ 
		System_abort("EasyLink_init failed");
	}	

    /* If you wich to use a frequency other than the default use
     * the below API
     * EasyLink_setFrequency(868000000);
     */

    /* Set concentrator address */;
    concentratorAddress = RADIO_CONCENTRATOR_ADDRESS;
    EasyLink_enableRxAddrFilter(&concentratorAddress, 1, 1);

    /* Set up Ack packet */
    ackPacket.header.sourceAddress = concentratorAddress;
    ackPacket.header.packetType = RADIO_PACKET_TYPE_ACK_PACKET;

    /* Enter receive */
    if(EasyLink_receiveAsync(rxDoneCallback, 0) != EasyLink_Status_Success) {
        System_abort("EasyLink_receiveAsync failed");
    }

    while (1) {
        uint32_t events = Event_pend(radioOperationEventHandle, 0, RADIO_EVENT_ALL, BIOS_WAIT_FOREVER);

        /* If valid packet received */
        if(events & RADIO_EVENT_VALID_PACKET_RECEIVED) {

            /* Send ack packet */
            sendAck(latestRxPacket.header.sourceAddress);

            /* Call packet received callback */
            notifyPacketReceived(&latestRxPacket);
            /* If all nodes has send the packet, wait for 30 seconds */
            if (counterNodes == number_nodes)
                    {
                        counterNodes = 0;
                        int i;
                        for (i = 0; i < MAX_NODES; i++) nodes[i] = 0;
                        time(&concentratorCurrentTime);
                        time_info = localtime(&concentratorCurrentTime);
                        while (time_info->tm_sec != 28 && time_info->tm_sec != 58)
                        {
                            time(&concentratorCurrentTime);
                            time_info = localtime(&concentratorCurrentTime);
                            sleep(1);
                        }
                    }

            /* Go back to RX */
            if(EasyLink_receiveAsync(rxDoneCallback, 0) != EasyLink_Status_Success) {
                System_abort("EasyLink_receiveAsync failed");
            }

            /* toggle Activity LED */
            PIN_setOutputValue(ledPinHandle, CONCENTRATOR_ACTIVITY_LED,
                    !PIN_getOutputValue(CONCENTRATOR_ACTIVITY_LED));
        }

        /* If invalid packet received */
        if(events & RADIO_EVENT_INVALID_PACKET_RECEIVED) {
            /* Go back to RX */
            if(EasyLink_receiveAsync(rxDoneCallback, 0) != EasyLink_Status_Success) {
                System_abort("EasyLink_receiveAsync failed");
            }
        }
    }
}

static void sendAck(uint8_t latestSourceAddress) {

    /* Set destinationAdress, but use EasyLink layers destination adress capability */
    txPacket.dstAddr[0] = latestSourceAddress;

    /* Copy ACK packet to payload, skipping the destination adress byte.
     * Note that the EasyLink API will implicitly both add the length byte and the destination address byte. */

    txPacket.payload[0] = ackPacket.header.sourceAddress;
    txPacket.payload[1] = ackPacket.header.packetType;
    txPacket.payload[2] = ackPacket.header.thirdAddress;
    txPacket.payload[3] = (ackPacket.header.clock & 0xFF000000) >> 24;
    txPacket.payload[4] = (ackPacket.header.clock & 0x00FF0000) >> 16;
    txPacket.payload[5] = (ackPacket.header.clock & 0xFF00) >> 8;
    txPacket.payload[6] = (ackPacket.header.clock & 0xFF);
    txPacket.len = sizeof(ackPacket);

    /* Send packet  */
    if (EasyLink_transmit(&txPacket) != EasyLink_Status_Success)
    {
        System_abort("EasyLink_transmit failed");
    }
}

static void notifyPacketReceived(union ConcentratorPacket* latestRxPacket)
{
    if (packetReceivedCallback)
    {
        packetReceivedCallback(latestRxPacket, latestRssi);
    }
}

static void rxDoneCallback(EasyLink_RxPacket * rxPacket, EasyLink_Status status)
{
    union ConcentratorPacket* tmpRxPacket;

    /* If we received a packet successfully */
    if (status == EasyLink_Status_Success)
    {
        /* Save the latest RSSI, which is later sent to the receive callback */
        latestRssi = (int8_t)rxPacket->rssi;

        /* Check that this is a valid packet */
        tmpRxPacket = (union ConcentratorPacket*)(rxPacket->payload);
        /* Convert the temperature in 4 bytes to float */
        float temperature = (rxPacket->payload[22] << 24) | (rxPacket->payload[23] << 16) | (rxPacket->payload[24] << 8) | rxPacket->payload[25];

        /* Convert the received data to an array for send to serial port */
        char buffer[256];
        sprintf(buffer, "%hd;%d;%d;%d;%d;%d;%d;%f", rxPacket->payload[2], rxPacket->payload[16], rxPacket->payload[17], rxPacket->payload[18], rxPacket->payload[19], rxPacket->payload[20], rxPacket->payload[21], temperature);
        Display_printf(display, 1, 0, "%s", buffer);

        /* If this is a known packet */
        if (tmpRxPacket->header.packetType == RADIO_PACKET_TYPE_ADC_SENSOR_PACKET)
        {
            /* Save packet */
            latestRxPacket.header.sourceAddress = rxPacket->payload[0];
            latestRxPacket.header.packetType = rxPacket->payload[1];
            latestRxPacket.header.thirdAddress = rxPacket->payload[2];
            latestRxPacket.adcSensorPacket.adcValue = (rxPacket->payload[7] << 8) | rxPacket->payload[8];

            /* Signal packet received */
            Event_post(radioOperationEventHandle, RADIO_EVENT_VALID_PACKET_RECEIVED);

            /* Prepare the ack packet */
            ackPacket.header.thirdAddress = rxPacket->payload[2];
            time(&concentratorCurrentTime);
            ackPacket.header.clock = *((uint32_t*)&concentratorCurrentTime); /* Save the clock of concentrator in ack packet */

            /* If is a new node, count the node and save the address */
            if (nodes[rxPacket->payload[2]] == 0)
            {
                counterNodes++;
                nodes[rxPacket->payload[2]] = rxPacket->payload[2];
            }
        }
        else if (tmpRxPacket->header.packetType == RADIO_PACKET_TYPE_DM_SENSOR_PACKET)
        {
            /* Save packet */
            latestRxPacket.header.sourceAddress = rxPacket->payload[0];
            latestRxPacket.header.packetType = rxPacket->payload[1];
            latestRxPacket.header.thirdAddress = rxPacket->payload[2];
            latestRxPacket.dmSensorPacket.adcValue = (rxPacket->payload[7] << 8) | rxPacket->payload[8];
            latestRxPacket.dmSensorPacket.batt = (rxPacket->payload[9] << 8) | rxPacket->payload[10];
            latestRxPacket.dmSensorPacket.time100MiliSec = (rxPacket->payload[11] << 24) |
                                                           (rxPacket->payload[12] << 16) |
                                                           (rxPacket->payload[13] << 8) |
                                                            rxPacket->payload[14];
            latestRxPacket.dmSensorPacket.button = rxPacket->payload[15];
            //latestRxPacket.dmSensorPacket.concLedToggle = rxPacket->payload[12];

            /* Signal packet received */
            Event_post(radioOperationEventHandle, RADIO_EVENT_VALID_PACKET_RECEIVED);

            /* Prepare the ack packet */
            ackPacket.header.thirdAddress = rxPacket->payload[2];
            time(&concentratorCurrentTime);
            ackPacket.header.clock = *((uint32_t*)&concentratorCurrentTime); /* Save the clock of concentrator in ack packet */

            /* If is a new node, count the node and save the address */
            if (nodes[rxPacket->payload[2]] == 0)
            {
                counterNodes++;
                nodes[rxPacket->payload[2]] = rxPacket->payload[2];
            }
        }
        else
        {
            /* Signal invalid packet received */
            Event_post(radioOperationEventHandle, RADIO_EVENT_INVALID_PACKET_RECEIVED);
        }
    }
    else
    {
        /* Signal invalid packet received */
        Event_post(radioOperationEventHandle, RADIO_EVENT_INVALID_PACKET_RECEIVED);
    }
}
