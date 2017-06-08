# wsync
Time synchronization of a wireless sensor network for precision agriculture

## Introduction
A theoretical intro to wireless time synchronization algorithms can be [found here](http://comp.ist.utl.pt/ece-wsn/doc/slides/sensys-ch8-Time-Synchronization.pdf)

A network of wireless sensor nodes based on the TI's [CC1350 Launchpad](http://www.ti.com/tool/LAUNCHXL-CC1350) is built to measure soil moisture in the field using a moisture probe connected to the CC1350 using the SDI-12 protocol. The nodes are connected using the microcontroller's Sub-1 GHz radio.

To save energy the nodes are kept in low power mode (with the **radio switched off**) most of the time.
Sampled data is locally timestamped in order to correlate the information gathered by all nodes in the network. Sampled data of all sensor nodes must be collected in a central station (called _sink_).

The main goal of the project is the development of a time synchronization protocol with the following properties:
1. Minimal drift in the timestamps of sampled data
2. Maximum power down time

## Software used
1. [CCSv7](http://processors.wiki.ti.com/index.php/Download_CCS). Code development using the C language.
  a. SimpleLink cc13x0 SDK.Sofwatre stack for Sub-1 GHz communications
  b. TI-RTOS. RTOS scheduler
2. [Doxygen](http://www.stack.nl/~dimitri/doxygen/). Code documentation
3. [GitKraken](https://www.gitkraken.com/) or [SourceTree](https://www.sourcetreeapp.com/). Git GUI
