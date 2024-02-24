# Hardware Specification

This document sets out the specification for the interface (connector at the end of the arm) between the robot arm and the effector.

It includes:

  * Measurements in metric for part placement
  * Components used

## Assumptions

The following assumptions are made when designing the interface:

  1. The interface will be mounted on a standard axel of either:
    a. A NEMA17 Stepper Motor
    b. An MG996R Metal Gear Servo
  2. Power and serial connection will be via a single, [4-pin magnetic connector](https://www.aliexpress.com/item/1005003687986489.html)
  3. The embedded controller on the effector will be an RP2040-based device, capable of receving serial data at 9600 baud
  4. The arm will be able to use the interface to change effectors automatically
