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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ad5245.h>
#include <ads1115.h>
#include <max31855.h>
#include <bms.h>
#include <mppt.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
FDCAN_HandleTypeDef hfdcan1;
FDCAN_HandleTypeDef hfdcan2;

I2C_HandleTypeDef hi2c2;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_FS;

/* USER CODE BEGIN PV */
static const uint16_t channels[] = {
    ADS1115_MUX_SINGLE_0,
    ADS1115_MUX_SINGLE_1,
    ADS1115_MUX_SINGLE_2,
    ADS1115_MUX_SINGLE_3,
};

static const char *channel_names[] = {
    "Input Current (AIN0)", "Output Current (AIN1)", "Input Voltage (AIN2)", "Output Voltage (AIN3)",
};

#define NUM_CHANNELS  (sizeof(channels) / sizeof(channels[0]))

static FDCAN_TxHeaderTypeDef fdcan2_txHeader;
static uint8_t fdcan2_txData[8];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_FDCAN2_Init(void);
static void MX_I2C2_Init(void);
static void MX_SPI1_Init(void);
static void MX_USB_PCD_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// Allows for you to do printf statements
int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart3, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}
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
  MX_FDCAN1_Init();
  MX_FDCAN2_Init();
  MX_I2C2_Init();
  MX_SPI1_Init();
  MX_USB_PCD_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  printf("alive\r\n");

  printf("Scanning I2C2...\r\n");
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
      if (HAL_I2C_IsDeviceReady(&hi2c2, (addr << 1), 3, 10) == HAL_OK) {
          printf("  Found device at 0x%02X\r\n", addr);
      }
  }
  printf("Done.\r\n");

  /* ---- ADS1115 Configuration ---- */
  	ADS1115_Config cfg = {
  		.mux       = ADS1115_MUX_SINGLE_0,
  		.pga       = ADS1115_PGA_4_096V,
  		.mode      = ADS1115_MODE_SINGLE,
  		.dr        = ADS1115_DR_128SPS,
  		.comp_mode = ADS1115_COMP_MODE_TRAD,
  		.comp_pol  = ADS1115_COMP_POL_LOW,
  		.comp_lat  = ADS1115_COMP_LAT_OFF,
  		.comp_que  = ADS1115_COMP_QUE_DISABLE,
  	};

  	HAL_StatusTypeDef status;

  	/* ---- Enable CAN transceiver before starting FDCAN ---- */
  	HAL_GPIO_WritePin(GPIOD, SD_Master_Pin, GPIO_PIN_SET);
  	HAL_GPIO_WritePin(GPIOD, SWEN_Master_Pin, GPIO_PIN_SET);
  	HAL_Delay(10);

  	/* ---- Start FDCAN2 (non-fatal for debugging) ---- */
  	if (HAL_FDCAN_Start(&hfdcan2) != HAL_OK) {
  		printf("FDCAN2 start FAILED\r\n");
  	} else {
  		printf("FDCAN2 started OK\r\n");
  	}

  	/* ---- Prepare FDCAN2 TX header (reused each send) ---- */
  	fdcan2_txHeader.Identifier          = 0x123;
  	fdcan2_txHeader.IdType              = FDCAN_STANDARD_ID;
  	fdcan2_txHeader.TxFrameType         = FDCAN_DATA_FRAME;
  	fdcan2_txHeader.DataLength          = FDCAN_DLC_BYTES_8;
  	fdcan2_txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  	fdcan2_txHeader.BitRateSwitch       = FDCAN_BRS_OFF;
  	fdcan2_txHeader.FDFormat            = FDCAN_CLASSIC_CAN;
  	fdcan2_txHeader.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
  	fdcan2_txHeader.MessageMarker       = 0;

  	/* ---- Initialize MPPT ---- */
  	MPPT_Config_t mppt_cfg = {
  	    .wiper_min     = 179,                    // your custom minimum
  	    .wiper_max     = 255,
  	    .po_step       = 1,
  	    .scan_step     = 5,
  	    .scan_interval = MPPT_SCAN_INTERVAL_DEFAULT,
  	};
  	MPPT_Init(&mppt_cfg);
  	printf("MPPT initialized\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	  // Heartbeat
	  HAL_GPIO_TogglePin(GPIOG, CAN_NMT_STATUS_Pin);
	  HAL_GPIO_TogglePin(GPIOG, CAN_ERROR_STATE_Pin);

	  // Set enable and switching pins high
	  HAL_GPIO_WritePin(GPIOD, SD_Master_Pin, GPIO_PIN_SET);
	  HAL_GPIO_WritePin(GPIOD, SWEN_Master_Pin, GPIO_PIN_SET);
	  HAL_GPIO_WritePin(GPIOD, SWEN_Slave_Pin, GPIO_PIN_RESET);
	  HAL_GPIO_WritePin(GPIOD, SD_Slave_Pin, GPIO_PIN_RESET);

//	  // Write to digital potentiometer
//	  status = AD5245_SetWiper(&hi2c2, AD5245_ADDR_AD0_LOW, 255);
//	  printf("Write Status: %d\r\n", status);

	  // MPPT + ADC + Print (every 50 ms)
	  static uint32_t last_mppt = 0;
	  if (HAL_GetTick() - last_mppt >= 200) {
		  last_mppt = HAL_GetTick();

		  int16_t raw;
		  float v_in = 0, i_in = 0, v_out = 0, i_out = 0;

		  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
			  cfg.mux = channels[ch];
			  if (ADS1115_ReadSingleShot(&hi2c2, ADS1115_ADDR_GND, &cfg, &raw) == HAL_OK) {
				  float mv = ADS1115_ConvertToMillivolts(raw, cfg.pga);
				  switch (ch) {
					  case 0: i_in  = (mv / 1000.0f / 50.0f) / 0.0025f; break;
					  case 1: i_out = (mv / 1000.0f / 50.0f) / 0.0025f; break;
					  case 2: v_in  = mv / 1000.0f * 20.0f;              break;
					  case 3: v_out = mv / 1000.0f * 20.0f;              break;
				  }
			  }
		  }

		  uint8_t wiper = MPPT_Step(v_in, i_in, v_out, i_out);
		  status = AD5245_SetWiper(&hi2c2, AD5245_ADDR_AD0_LOW, 230);

		  const MPPT_Data_t *mppt = MPPT_GetData();
		  printf("MPPT: tick=%lu  wiper=%u  state=%d  Pin=%.2fW  Vin=%.2fV  Iin=%.3fA\r\n",
				 HAL_GetTick(), 230, mppt->state,
				 mppt->power_W, mppt->in_voltage_V, mppt->in_current_A);
		  printf("      Vout=%.2fV  Iout=%.3f A\r\n",
				 mppt->out_voltage_V, mppt->out_current_A);
		  printf("----\r\n");
	  }

	  //	  // Thermocouple reads (uncomment when needed)
	  //	  static uint32_t last_tc = 0;
	  //	  if (HAL_GetTick() - last_tc >= 1000) {
	  //		  last_tc = HAL_GetTick();
	  //
	  //		  MAX31855_Data tc;
	  //		  if (MAX31855_Read(&hspi1, THERMO_CS1_GPIO_Port, THERMO_CS1_Pin, &tc) == HAL_OK && !tc.fault) {
	  //			  printf("TC3: %.2f C  |  CJ3: %.2f C\r\n", tc.tc_temp, tc.cj_temp);
	  //		  }
	  //
	  //		  if (MAX31855_Read(&hspi1, GPIOD, THERMO_CS2_Pin, &tc) == HAL_OK && !tc.fault) {
	  //			  printf("TC2: %.2f C  |  CJ2: %.2f C\r\n", tc.tc_temp, tc.cj_temp);
	  //		  }
	  //
	  //		  if (MAX31855_Read(&hspi1, GPIOD, THERMO_CS3_Pin, &tc) == HAL_OK && !tc.fault) {
	  //			  printf("TC1: %.2f C  |  CJ1: %.2f C\r\n", tc.tc_temp, tc.cj_temp);
	  //		  }
	  //	  }


//	status = MAX31855_Read(&hspi1, THERMO_CS1_GPIO_Port, THERMO_CS1_Pin, &tc);	// Read from J_Temp3
//	if (status != HAL_OK) {
//		printf("SPI ERROR (0x%02X)\r\n", status);
//	} else if (tc.fault) {
//		printf("FAULT: SCV=%d SCG=%d OC=%d\r\n",
//		tc.fault_scv, tc.fault_scg, tc.fault_oc);
//	} else {
//		printf("TC3: %8.2f C  |  CJ3: %8.2f C\r\n", tc.tc_temp, tc.cj_temp);
//	}
//
//	status = MAX31855_Read(&hspi1, GPIOD, THERMO_CS2_Pin, &tc);	// Read from J_Temp2
//	if (status != HAL_OK) {
//		printf("SPI ERROR (0x%02X)\r\n", status);
//	} else if (tc.fault) {
//		printf("FAULT: SCV=%d SCG=%d OC=%d\r\n",
//		tc.fault_scv, tc.fault_scg, tc.fault_oc);
//	} else {
//		printf("TC2: %8.2f C  |  CJ2: %8.2f C\r\n", tc.tc_temp, tc.cj_temp);
//	}
//
//	status = MAX31855_Read(&hspi1, GPIOD, THERMO_CS3_Pin, &tc);	// Read from J_Temp1
//	if (status != HAL_OK) {
//		printf("SPI ERROR (0x%02X)\r\n", status);
//	} else if (tc.fault) {
//		printf("FAULT: SCV=%d SCG=%d OC=%d\r\n",
//		tc.fault_scv, tc.fault_scg, tc.fault_oc);
//	} else {
//		printf("TC1: %8.2f C  |  CJ1: %8.2f C\r\n", tc.tc_temp, tc.cj_temp);
//	}

//	/* ---- Send FDCAN2 message ---- */
//	fdcan2_txData[0] = 0x01;
//	fdcan2_txData[1] = 0x02;
//	fdcan2_txData[2] = 0x03;
//	fdcan2_txData[3] = 0x04;
//	fdcan2_txData[4] = 0x05;
//	fdcan2_txData[5] = 0x06;
//	fdcan2_txData[6] = 0x07;
//	fdcan2_txData[7] = 0x08;

//	if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &fdcan2_txHeader, fdcan2_txData) != HAL_OK) {
//		printf("FDCAN2 TX failed\r\n");
//	} else {
//		printf("FDCAN2 TX OK (ID=0x%03lX)\r\n", fdcan2_txHeader.Identifier);
//	}


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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 12;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV4;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */

  /* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = DISABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 3;
  hfdcan1.Init.NominalSyncJumpWidth = 1;
  hfdcan1.Init.NominalTimeSeg1 = 13;
  hfdcan1.Init.NominalTimeSeg2 = 2;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.StdFiltersNbr = 0;
  hfdcan1.Init.ExtFiltersNbr = 0;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */

  /* USER CODE END FDCAN1_Init 2 */

}

/**
  * @brief FDCAN2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN2_Init(void)
{

  /* USER CODE BEGIN FDCAN2_Init 0 */

  /* USER CODE END FDCAN2_Init 0 */

  /* USER CODE BEGIN FDCAN2_Init 1 */

  /* USER CODE END FDCAN2_Init 1 */
  hfdcan2.Instance = FDCAN2;
  hfdcan2.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan2.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan2.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan2.Init.AutoRetransmission = DISABLE;
  hfdcan2.Init.TransmitPause = DISABLE;
  hfdcan2.Init.ProtocolException = DISABLE;
  hfdcan2.Init.NominalPrescaler = 12;
  hfdcan2.Init.NominalSyncJumpWidth = 1;
  hfdcan2.Init.NominalTimeSeg1 = 13;
  hfdcan2.Init.NominalTimeSeg2 = 2;
  hfdcan2.Init.DataPrescaler = 1;
  hfdcan2.Init.DataSyncJumpWidth = 1;
  hfdcan2.Init.DataTimeSeg1 = 1;
  hfdcan2.Init.DataTimeSeg2 = 1;
  hfdcan2.Init.StdFiltersNbr = 0;
  hfdcan2.Init.ExtFiltersNbr = 0;
  hfdcan2.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN2_Init 2 */

  /* USER CODE END FDCAN2_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x00503D58;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 250000;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_PCD_Init(void)
{

  /* USER CODE BEGIN USB_Init 0 */

  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */

  /* USER CODE END USB_Init 1 */
  hpcd_USB_FS.Instance = USB;
  hpcd_USB_FS.Init.dev_endpoints = 8;
  hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_FS.Init.battery_charging_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */

  /* USER CODE END USB_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, CAN_NMT_STATUS_Pin|CAN_ERROR_STATE_Pin|GPIO_PIN_7|GPIO_PIN_8
                          |GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(THERMO_CS1_GPIO_Port, THERMO_CS1_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, THERMO_CS2_Pin|THERMO_CS3_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, SWEN_Master_Pin|SWEN_Slave_Pin|SD_Master_Pin|SD_Slave_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PE2 PE3 PE0 PE1 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : PE4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : PF3 PF4 PF5 PF7 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pins : CAN_NMT_STATUS_Pin CAN_ERROR_STATE_Pin PG7 PG8
                           PG9 */
  GPIO_InitStruct.Pin = CAN_NMT_STATUS_Pin|CAN_ERROR_STATE_Pin|GPIO_PIN_7|GPIO_PIN_8
                          |GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pins : PG3 PG4 PG5 PG6 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : THERMO_CS1_Pin */
  GPIO_InitStruct.Pin = THERMO_CS1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(THERMO_CS1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PC10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : THERMO_CS2_Pin THERMO_CS3_Pin SWEN_Master_Pin SWEN_Slave_Pin
                           SD_Master_Pin SD_Slave_Pin */
  GPIO_InitStruct.Pin = THERMO_CS2_Pin|THERMO_CS3_Pin|SWEN_Master_Pin|SWEN_Slave_Pin
                          |SD_Master_Pin|SD_Slave_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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
