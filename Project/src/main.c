#include <stdint.h>
#include "stm32f4_discovery.h"
#include "stm32f4xx.h"
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"
#include "stm32f4xx_adc.h"
#include "stm32f4xx_gpio.h"

#define Tmax 10000
#define Tmin 2000
#define Tyellow 4000

#define MAX_POT_VALUE 4095

#define RED GPIO_Pin_0
#define YELLOW GPIO_Pin_1
#define GREEN GPIO_Pin_2

#define POT GPIO_Pin_3

#define DATA GPIO_Pin_6
#define CLOCK GPIO_Pin_7
#define RESET GPIO_Pin_8

void vGPIO_Init();
void vADC_Init();

static void prvSetupHardware(void);

uint16_t usADC_GetValue();

void vTrafficFlowAdjustmentTask(void *pvParameters);
void vTrafficGeneratorTask(void *pvParameters);
void vDisplayTask(void *pvParameters);

QueueHandle_t xFlowQueue;
QueueHandle_t xTrafficLightQueue;
QueueHandle_t xTrafficQueue;

TimerHandle_t xRedTimer;
TimerHandle_t xYellowTimer;
TimerHandle_t xGreenTimer;

void vRedTimerCallback(TimerHandle_t xTimer);
void vYellowTimerCallback(TimerHandle_t xTimer);
void vGreenTimerCallback(TimerHandle_t xTimer);

int main(void) {
  vGPIO_Init();
  vADC_Init();

  prvSetupHardware();

  // Create tasks
  xTaskCreate(vTrafficFlowAdjustmentTask , "Traffic Flow Adjustment Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
  xTaskCreate(vTrafficGeneratorTask, "Traffic Generator Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
  xTaskCreate(vDisplayTask, "Display Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

  // Create queues
  xFlowQueue = xQueueCreate(1, sizeof(uint8_t));
  xTrafficQueue = xQueueCreate(1, sizeof(uint8_t));
  xTrafficLightQueue = xQueueCreate(1, sizeof(uint16_t));

  // Create timers
  xRedTimer = xTimerCreate("Red Timer", pdMS_TO_TICKS(Tmax), pdFALSE, 0, vRedTimerCallback);
  xYellowTimer = xTimerCreate("Yellow Timer", pdMS_TO_TICKS(Tyellow), pdFALSE, 0, vYellowTimerCallback);
  xGreenTimer = xTimerCreate("Green Timer", pdMS_TO_TICKS(Tmin), pdFALSE, 0, vGreenTimerCallback);

  // Start the green timer and overwrite queue with state
  xTimerStart(xGreenTimer, 0);
  uint16_t usTrafficLight = GREEN;
  xQueueOverwrite(xTrafficLightQueue, &usTrafficLight);

  vTaskStartScheduler();

  return 0;
}

void vGPIO_Init() {
  GPIO_InitTypeDef GPIO_InitStruct;

  // Enable clock
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

  // Set configuration for traffic lights
  GPIO_InitStruct.GPIO_Pin = RED | YELLOW | GREEN;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;

  // Init
  GPIO_Init(GPIOC, &GPIO_InitStruct);

  // Set configuration for potentiometer
  GPIO_InitStruct.GPIO_Pin = POT;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AN;
  GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;

  // Init
  GPIO_Init(GPIOC, &GPIO_InitStruct);

  // Set configration for data, reset, and clock (used for shift registers)
  GPIO_InitStruct.GPIO_Pin = DATA | CLOCK | RESET;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;

  // Init
  GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void vADC_Init() {
  ADC_InitTypeDef ADC_InitStruct;

  // Enable clock
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

  // Set configuration
  ADC_InitStruct.ADC_DataAlign = ADC_DataAlign_Right;
  ADC_InitStruct.ADC_Resolution = ADC_Resolution_12b;
  ADC_InitStruct.ADC_ContinuousConvMode = DISABLE;
  ADC_InitStruct.ADC_ScanConvMode = DISABLE;
  ADC_InitStruct.ADC_ExternalTrigConv = DISABLE;

  // Init
  ADC_Init(ADC1, &ADC_InitStruct);

  // Enable ADC
  ADC_Cmd(ADC1, ENABLE);

  // Set channel (PC3 corresponds to ADC_Channel_13)
  ADC_RegularChannelConfig(ADC1, ADC_Channel_13, 1, ADC_SampleTime_3Cycles);
}

uint16_t usADC_GetValue() {
  // Start conversion
  ADC_SoftwareStartConv(ADC1);

  // Wait until EOC bit is set
  while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

  // Return value
  return ADC_GetConversionValue(ADC1);
}

// Called when the green light timer expires
void vGreenTimerCallback(TimerHandle_t xTimer) {
  //Start timer for yellow light
  xTimerStart(xYellowTimer, 0);

  // Update traffic light state
  uint16_t usTrafficLight = YELLOW;
  xQueueOverwrite(xTrafficLightQueue, &usTrafficLight);
}

// Called when the yellow light timer expires
void vYellowTimerCallback(TimerHandle_t xTimer) {
  uint8_t ucFlow;

  // Check the flow rate
  if (xQueuePeek(xFlowQueue, &ucFlow, pdMS_TO_TICKS(100))) {
    // Adjust period of red timer (also starts the timer)
    xTimerChangePeriod(xRedTimer, pdMS_TO_TICKS(Tmax / ucFlow + 2000), 0); // Inversely proportional

    // Update traffic light state
    uint16_t usTrafficLight = RED;
    xQueueOverwrite(xTrafficLightQueue, &usTrafficLight);
  }
}

// Called when the red light timer expires
void vRedTimerCallback(TimerHandle_t xTimer) {
  uint8_t ucFlow;

  // Check the flow rate
  if (xQueuePeek(xFlowQueue, &ucFlow, pdMS_TO_TICKS(100))) {
    // Adjust period of green timer (also starts the timer)
    xTimerChangePeriod(xGreenTimer, pdMS_TO_TICKS(Tmax * ucFlow / 100 + 2000), 0); // Directly proportional

    // Update traffic light state
    uint16_t usTrafficLight = GREEN;
    xQueueOverwrite(xTrafficLightQueue, &usTrafficLight);
  }
}

void vTrafficFlowAdjustmentTask(void *pvParameters) {
  while(1) {
    // Get value from ADC and scale between 1 and 100
    uint8_t ucFlow = 1 + ((usADC_GetValue() * 99) / MAX_POT_VALUE); // 0 to 99 plus 1

    // Overwrite flow rate in the queue
    if (xQueueOverwrite(xFlowQueue, &ucFlow)) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

void vTrafficGeneratorTask(void *pvParameters) {
  while(1) {
    uint8_t ucFlow;

    // Check the flow rate
    if (xQueuePeek(xFlowQueue, &ucFlow, pdMS_TO_TICKS(100))) { // Only peek because other tasks also need this value
      // Randomly determine if a car is generated (random value from -15 to 85)
      if (( (rand() % 100) - 15) < ucFlow) { // Higher flow means higher probability
        uint8_t ucCar = 1;
        xQueueSend(xTrafficQueue, &ucCar, pdMS_TO_TICKS(100));
      }

      // Delay between generating cars
      uint8_t ucDelay = 1000 / ucFlow;
      vTaskDelay(pdMS_TO_TICKS(ucDelay));
    }
  }
}

void vDisplayTask(void *pvParameters) {
  uint8_t aucCarPositions[19] = {0}; // 19 LEDs, each of which represents a car position

  // Reset the shift register
  GPIO_SetBits(GPIOC, RESET);

  while(1) {
    uint8_t ucCar = 0;

    // Try to get a car from the queue
    xQueueReceive(xTrafficQueue, &ucCar, pdMS_TO_TICKS(100));

    // Put car into car array (or it gets left blank if no car received)
    aucCarPositions[0] = ucCar;

    // Display the cars in their respective positions
    for (int i = 18; i >= 0; i--) {
      if (aucCarPositions[i]) {
        GPIO_SetBits(GPIOC, DATA); // LED on (car is in this position)
      } else {
        GPIO_ResetBits(GPIOC, DATA); // LED off (no car in this position)
      }

      GPIO_SetBits(GPIOC, CLOCK);
      GPIO_ResetBits(GPIOC, CLOCK);
    }

    // Get the traffic light state
    uint16_t usTrafficLight;
    if (xQueuePeek(xTrafficLightQueue, &usTrafficLight, pdMS_TO_TICKS(100))) {

      // Set the traffic lights (reset first)
      GPIO_ResetBits(GPIOC, RED);
      GPIO_ResetBits(GPIOC, GREEN);
      GPIO_ResetBits(GPIOC, YELLOW);
      GPIO_SetBits(GPIOC, usTrafficLight);

      // Move cars on green light
      if (usTrafficLight == GREEN) {
        for (int i = 18; i > 0; i--) {
          aucCarPositions[i] = aucCarPositions[i - 1]; // Shift right
        }
        aucCarPositions[0] = 0; // Index 0 is now empty
      }

      // When light is yellow or red
      else {
        // Move cars that are past stop line
        for (int i = 18; i > 8; i--) {
          aucCarPositions[i] = aucCarPositions[i - 1]; // Shift right
        }
        aucCarPositions[8] = 0; // Index 8 is now empty

        // Move the rest up to stop line
        for (int i = 7; i > 0; i--) {
          if (!aucCarPositions[i]) {
            aucCarPositions[i] = aucCarPositions[i - 1]; // Shift right only if position is available
            aucCarPositions[i - 1] = 0; // Previous position is now empty
          }
        }
        aucCarPositions[0] = 0; // Index 0 is now empty
      }
    }

    // Repeat approximately every second
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

static void prvSetupHardware(void) {
  NVIC_SetPriorityGrouping(0);
}

void vApplicationMallocFailedHook() {}
void vApplicationStackOverflowHook() {}
void vApplicationIdleHook() {}
