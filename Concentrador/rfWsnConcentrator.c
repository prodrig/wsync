/*
 *  ======== rfWsnconcentrator.c ========
 */
/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>

/* TI-RTOS Header files */
#include <ti/display/Display.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/SPI.h>

/* Board Header files */
#include "Board.h"

/* Application Header files */ 
#include "ConcentratorRadioTask.h"
#include "ConcentratorTask.h"

/*
 *  ======== main ========
 */
int main(void)
{
    /* Call driver init functions. */
    Board_initGeneral();

    UART_init();

    /* Initialize concentrator tasks */
    ConcentratorRadioTask_init();
    ConcentratorTask_init();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
