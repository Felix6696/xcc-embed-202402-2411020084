/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE BEGIN PD */

/* 1：低电平点亮；0：高电平点亮 */
#define LED_ACTIVE_LOW       1U

#define LED_ALL_PINS         (GPIO_PIN_0 | GPIO_PIN_1 | \
                              GPIO_PIN_2 | GPIO_PIN_3)

#define KEY_ROW_ALL_PINS     (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6)

#define KEY_NONE             0U
#define KEY_INVALID          0xFFU
#define KEY_DEBOUNCE_MS       20U

/* USER CODE END PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE BEGIN 0 */

static const uint16_t key_row_pins[3] =
{
    GPIO_PIN_4,
    GPIO_PIN_5,
    GPIO_PIN_6
};

static const uint16_t key_col_pins[4] =
{
    GPIO_PIN_0,
    GPIO_PIN_1,
    GPIO_PIN_2,
    GPIO_PIN_3
};

static uint8_t display_value = 0U;
static uint32_t display_start_tick = 0U;


/*
 * 写入 LED 的逻辑亮灭状态：
 * bit0 -> PF0
 * bit1 -> PF1
 * bit2 -> PF2
 * bit3 -> PF3
 *
 * 位为 1 表示点亮，与实际有效电平无关。
 */
static void LED_WriteBits(uint8_t bits)
{
    uint32_t i;

    for (i = 0U; i < 4U; i++)
    {
        uint16_t pin = (uint16_t)(GPIO_PIN_0 << i);
        GPIO_PinState state;

        if ((bits & (1U << i)) != 0U)
        {
            state = (LED_ACTIVE_LOW != 0U)
                    ? GPIO_PIN_RESET : GPIO_PIN_SET;
        }
        else
        {
            state = (LED_ACTIVE_LOW != 0U)
                    ? GPIO_PIN_SET : GPIO_PIN_RESET;
        }

        HAL_GPIO_WritePin(GPIOF, pin, state);
    }
}


/* 显示一位十进制数，允许范围为 0～9。 */
static void LED_ShowDigit(uint8_t digit)
{
    if (digit <= 9U)
    {
        LED_WriteBits(digit);
    }
    else
    {
        LED_WriteBits(0U);
    }
}


/*
 * 完成一轮 3 行扫描。
 *
 * 返回值：
 * 0    ：没有按键
 * 1～12：检测到一个按键
 * 0xFF ：检测到多个闭合位置，视为无效
 *
 * 本实验只接受单键操作。
 */
static uint8_t Keypad_ScanRaw(void)
{
    uint8_t row;
    uint8_t col;
    uint8_t count = 0U;
    uint8_t key = KEY_NONE;

    /* 开漏输出写 SET，释放所有行。 */
    HAL_GPIO_WritePin(GPIOE,
                      KEY_ROW_ALL_PINS,
                      GPIO_PIN_SET);

    for (row = 0U; row < 3U; row++)
    {
        /* 当前行拉低。 */
        HAL_GPIO_WritePin(GPIOE,
                          key_row_pins[row],
                          GPIO_PIN_RESET);

        /*
         * 等待信号稳定。
         * 教学实验采用 1 ms，方便直接使用 HAL。
         */
        HAL_Delay(1U);

        for (col = 0U; col < 4U; col++)
        {
            if (HAL_GPIO_ReadPin(GPIOE,
                                 key_col_pins[col]) == GPIO_PIN_RESET)
            {
                count++;
                key = (uint8_t)(row * 4U + col + 1U);
            }
        }

        /* 当前行扫描结束，立即释放。 */
        HAL_GPIO_WritePin(GPIOE,
                          key_row_pins[row],
                          GPIO_PIN_SET);
    }

    if (count == 0U)
    {
        return KEY_NONE;
    }

    if (count == 1U)
    {
        return key;
    }

    return KEY_INVALID;
}


/*
 * 获取经过消抖的“新按下”事件。
 *
 * 连续采样保持相同状态至少 20 ms，才认可该状态。
 * 长按只返回一次键值。
 * 再次触发前，需要所有按键稳定松开。
 */
static uint8_t Keypad_GetEvent(void)
{
    static uint8_t candidate = KEY_NONE;
    static uint8_t stable = KEY_NONE;
    static uint8_t armed = 1U;
    static uint32_t candidate_tick = 0U;

    uint8_t raw = Keypad_ScanRaw();
    uint32_t now = HAL_GetTick();

    if (raw != candidate)
    {
        candidate = raw;
        candidate_tick = now;
        return KEY_NONE;
    }

    if ((uint32_t)(now - candidate_tick) < KEY_DEBOUNCE_MS)
    {
        return KEY_NONE;
    }

    if (stable == candidate)
    {
        return KEY_NONE;
    }

    stable = candidate;

    if (stable == KEY_NONE)
    {
        /* 稳定松开后，允许下一次按键事件。 */
        armed = 1U;
        return KEY_NONE;
    }

    if (stable == KEY_INVALID)
    {
        /* 多键按下后，要求全部松开再继续。 */
        armed = 0U;
        return KEY_NONE;
    }

    if (armed != 0U)
    {
        armed = 0U;
        return stable;
    }

    return KEY_NONE;
}


/* 保存需要显示的键值，并重置两位数的显示周期。 */
static void Display_SetValue(uint8_t value)
{
    display_value = value;
    display_start_tick = HAL_GetTick();
}


/*
 * 更新显示。
 *
 * 0～9：持续显示一位 BCD。
 *
 * 10～12：以 2400 ms 为一个周期：
 *   0～699 ms    显示十位
 *   700～899 ms  熄灭，分隔
 *   900～1599 ms 显示个位
 *   1600～2399 ms 熄灭，等待下一轮
 *
 * 显示计时不使用长延时，主循环仍可继续扫描按键。
 */
static void Display_Task(void)
{
    uint32_t phase;

    if (display_value <= 9U)
    {
        LED_ShowDigit(display_value);
        return;
    }

    phase = (uint32_t)(HAL_GetTick() - display_start_tick) % 2400U;

    if (phase < 700U)
    {
        LED_ShowDigit((uint8_t)(display_value / 10U));
    }
    else if (phase < 900U)
    {
        LED_WriteBits(0U);
    }
    else if (phase < 1600U)
    {
        LED_ShowDigit((uint8_t)(display_value % 10U));
    }
    else
    {
        LED_WriteBits(0U);
    }
}

/* USER CODE END 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */

/* USER CODE BEGIN 2 */

/* 释放全部扫描行。 */
HAL_GPIO_WritePin(GPIOE,
                  KEY_ROW_ALL_PINS,
                  GPIO_PIN_SET);

/* 上电显示 0，即四个 LED 全灭。 */
Display_SetValue(0U);
LED_ShowDigit(0U);

/* USER CODE END 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  uint8_t key = Keypad_GetEvent();

    if (key != KEY_NONE)
    {
        Display_SetValue(key);
    }

    Display_Task();

    HAL_Delay(1U);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
