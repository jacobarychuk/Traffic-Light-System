# Traffic Light System

## Overview
This system simulates vehicle traffic on a one-way, one-lane road with a simplified intersection controlled by a single traffic light. The system utilizes FreeRTOS features such as tasks, queues, and software timers to manage execution and communication efficiently. A system overview diagram illustrating the relationships between tasks and queues is provided below. 

Tasks are represented as blue rectangles, functions are represented as green ovals, and queues are represented as yellow arrows. The sender to the queue is at the tail of the arrow, while the receiver is at the head.

<img width="1324" height="531" alt="system_overview_diagram" src="https://github.com/user-attachments/assets/adf69f5e-3281-4a16-80a1-71592cde2d0d" />

As illustrated in the diagram, the system consists of three primary tasks:

1. **Traffic Flow Adjustment Task:** Uses a potentiometer to dynamically adjust the traffic flow rate

2. **Traffic Generator Task:** Randomly generates cars at a rate that is directly proportional to the traffic flow rate and potentiometer resistance, then sends them to the display

3. **Display Task:** Manages the on-off states of the LEDs to visualize vehicle movement and traffic light status

## Hardware Components
The following hardware components are used in the system:

* **Potentiometer:** Adjusts traffic flow

* **LEDs:** Represent vehicles and traffic lights
  
* **Shift Registers:** Control traffic flow by extending GPIO outputs
  
* **STM32 Microcontroller:** Processes all tasks and manages communication

## GPIO Configuration
The system uses GPIOC where the following pins are programmed according to the configuration shown in the table below.

| Pin | Mode | Purpose |
|-----|--------|----------------------|
| PC0 | Output | Red Light |
| PC1 | Output | Yellow Light |
| PC2 | Output | Green Light |
| PC3 | Analog | Potentiometer Input |
| PC6 | Output | Shift Register Data |
| PC7 | Output | Shift Register Clock |
| PC8 | Output | Shift Register Reset |

## Communication & Data

**Traffic Flow Adjustment Task** obtains the potentiometer resistance using the ADC, then scales it to a value between 1 and 100 representing the flow rate as a percentage. This value is sent to the **Flow Queue** by overwriting its existing value. Since both the **Traffic Generator Task** and the callbacks for the traffic light timers require the flow rate, they must only peek at the value to ensure that the other can also access it.

**Traffic Generator Task** uses the flow rate to randomly generate cars. For each car that is generated, it writes a 1 to the **Traffic Queue** so that **Display Task** can access it.

**Display Task** uses an array to track car positions. If it successfully receives a value from the **Traffic Queue**, then a new car is displayed. Since the callbacks for the traffic light timers update the **Traffic Light Queue** with the current traffic light state, the **Display Task** also obtains the traffic light to turn on from this queue.

The relationship between queues and tasks is summarized in the table below. All queues are of length-one.

| Queue | Sender | Receiver | Data Type | Values |
|-------|--------|----------|-----------|--------|
| Flow Queue | Traffic Flow Adjustment Task | Traffic Generator Task | int | [1, 100] |
| Traffic Queue | Traffic Generator Task | Display Task | int | 1 |
| Traffic Light Queue | Timer Callback | Display Task | uint16_t | GPIO_Pin_0/1/2 |

## Traffic Light Timing
The traffic lights are managed using FreeRTOS timers with the following configuration:

* **Green Light Timer:** Starts with the minimum duration of 2 seconds, increasing towards a maximum of 10 seconds as traffic flow increases
  
* **Yellow Light Timer:** Maintains a fixed duration of 4 seconds
  
* **Red Light Timer:** Starts with the maximum duration of 10 seconds, decreasing towards a minimum of 2 seconds as traffic flow increases

## Traffic Generation
Traffic generation is random and proportional to the potentiometer resistance. If the flow rate is successfully obtained from **Flow Queue**, then a random number is generated in the range of -15 to 85. A car is generated only when the random number is less than the flow rate. A higher potentiometer resistance results in a higher flow rate, increasing the probability that the generated random number is below the flow rate and therefore causing cars to be generated more frequently. This algorithm is illustrated in the diagram below.

<img width="282" height="734" alt="traffic_generation_diagram" src="https://github.com/user-attachments/assets/96b93abd-ef8b-49b9-b5f9-effe187f0829" />

## Display
An array of 19 elements represents all possible car positions along the LED strip, with 1 representing a car and 0 indicating an empty space. When the **Display Task** receives a new car from the **Traffic Queue**, it is placed at the first position, then it proceeds to shift all elements of the array rightwards by one index to simulate traffic flow.

Traffic lights are controlled based on the state received from **Traffic Light Queue**. When the light is green, cars move forward freely by shifting all elements in the array to the right. When the light is yellow or red, cars before the stop line remain stationary, while those past the stop line (i.e., index 8 onwards) continue moving forward.

This algorithm is illustrated in the diagram below.

<img width="314" height="781" alt="display_algorithm" src="https://github.com/user-attachments/assets/438267e0-ae63-40a4-8675-6282a9855b57" />
