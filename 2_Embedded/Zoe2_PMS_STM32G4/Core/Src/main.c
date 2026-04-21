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
#include <stdbool.h>
#include <math.h>
#include <ad5245.h>
#include <ads1115.h>
#include <max31855.h>
#include <bms.h>
#include <mppt.h>
#include <safety.h>
#include <ssd1306.h>
#include <ssd1306_fonts.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Set to 1 to enable I2C bus scan at boot (adds ~400 ms delay) */
#define DEBUG_I2C_SCAN    0
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

#define NUM_CHANNELS  (sizeof(channels) / sizeof(channels[0]))

/* ADS1115 runtime config (mux updated per-channel in read_adc) */
static ADS1115_Config cfg;

/* Latest thermocouple readings, cached for OLED and safety module.
 * Each TC has a value and a "last good" timestamp (HAL tick ms).
 * Readings older than TC_STALE_MS are considered stale.
 * Initial ts=0 means "never read" — treated as stale. */
#define TC_STALE_MS   3000u   /* 3x the 1000 ms read period */
static float    g_tc_C[3]     = { 0.0f, 0.0f, 0.0f };
static uint32_t g_tc_last[3]  = { 0, 0, 0 };
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
static void read_adc(float *v_in, float *i_in, float *v_out, float *i_out);
static void read_thermocouples(void);
static void print_telemetry(const MPPT_Data_t *m);
static void update_oled(const MPPT_Data_t *m);
static void boost_master_enable(bool on);
static void boost_slave_enable(bool on);
static void boost_disable_all(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// Allows for you to do printf statements
int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart3, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

/**
 * @brief Read all four ADS1115 channels and convert to engineering units.
 *
 * Channel mapping:
 *   AIN0 -> input current  (50x gain, 2.5 mOhm shunt)
 *   AIN1 -> output current (50x gain, 2.5 mOhm shunt)
 *   AIN2 -> input voltage  (1:20 divider)
 *   AIN3 -> output voltage (1:20 divider)
 *
 * On I2C error the corresponding output is left at 0.
 */
static void read_adc(float *v_in, float *i_in, float *v_out, float *i_out)
{
    int16_t raw;
    *v_in = *i_in = *v_out = *i_out = 0.0f;

    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
        cfg.mux = channels[ch];
        if (ADS1115_ReadSingleShot(&hi2c2, ADS1115_ADDR_GND, &cfg, &raw) == HAL_OK) {
            float mv = ADS1115_ConvertToMillivolts(raw, cfg.pga);
            switch (ch) {
                case 0: *i_in  = (mv / 1000.0f / 50.0f) / 0.0025f; break;
                case 1: *i_out = (mv / 1000.0f / 50.0f) / 0.0025f; break;
                case 2: *v_in  = mv / 1000.0f * 20.0f;             break;
                case 3: *v_out = mv / 1000.0f * 20.0f;             break;
            }
        }
    }
}

/**
 * @brief Read all three MAX31855 thermocouples and update the cache.
 *
 * Wiring note (historical): CS1 drives J_Temp3, CS2 drives J_Temp2,
 * CS3 drives J_Temp1. This helper presents them as TC1/TC2/TC3 in
 * the cache to match physical labels.
 *
 * On success, the reading and HAL tick are written to g_tc_C[] and
 * g_tc_last[]. On SPI error or sensor fault the entry is left alone
 * — update_oled() will age out via TC_STALE_MS and display "---".
 */
static void read_thermocouples(void)
{
    /* Mapping: cache index -> (GPIO port, pin) */
    static const struct {
        GPIO_TypeDef *port;
        uint16_t      pin;
    } tc_cs[3] = {
        { GPIOD,                 THERMO_CS3_Pin },   /* TC1 */
        { GPIOD,                 THERMO_CS2_Pin },   /* TC2 */
        { THERMO_CS1_GPIO_Port,  THERMO_CS1_Pin },   /* TC3 */
    };

    uint32_t now = HAL_GetTick();
    MAX31855_Data tc;

    for (uint8_t i = 0; i < 3; i++) {
        if (MAX31855_Read(&hspi1, tc_cs[i].port, tc_cs[i].pin, &tc) == HAL_OK
            && !tc.fault) {
            g_tc_C[i]    = tc.tc_temp;
            g_tc_last[i] = now;
            printf("TC%u: %.2f C  |  CJ%u: %.2f C\r\n",
                   i + 1, tc.tc_temp, i + 1, tc.cj_temp);
        } else {
            printf("TC%u: read failed or faulted\r\n", i + 1);
        }
    }
}

/**
 * @brief Print one MPPT telemetry record to the debug UART.
 */
static void print_telemetry(const MPPT_Data_t *m)
{
    printf("MPPT: tick=%lu  wiper=%u  state=%d  Pin=%.2fW  Vin=%.2fV  Iin=%.3fA\r\n",
           HAL_GetTick(), m->wiper, m->state,
           m->power_W, m->in_voltage_V, m->in_current_A);
    printf("      Vout=%.2fV  Iout=%.3f A\r\n",
           m->out_voltage_V, m->out_current_A);
    printf("----\r\n");
}

/**
 * @brief Render telemetry to a dual-color SSD1306 OLED.
 *
 * Display physically has yellow pixels on rows 0-15 and white/blue
 * pixels on rows 16-63. Layout exploits this split:
 *
 *   YELLOW BAND (y=0..15):
 *     y=3    "PMS         TRACK"  Font_7x10 (18 chars, 10px tall)
 *            — when safety trips, the tag replaces the MPPT state.
 *
 *   WHITE AREA (y=16..63):
 *     y=18   "Pin  : 39.3 W"
 *     y=27   "Pout : 29.0 W"
 *     y=36   "Eff  :  78 %"
 *     y=45   "TC1:45 TC2:42 TC3:68"
 *     y=54   "W:230"
 *
 * Frame push takes ~20-25 ms @ 400 kHz I2C. Call no faster than 500 ms.
 */
static void update_oled(const MPPT_Data_t *m)
{
    static const char *state_names[] = { "IDLE", "TRACK", "SCAN", "FAULT" };
    char buf[24];

    ssd1306_Fill(Black);

    /* ── YELLOW BAND: project + state (or safety tag if faulted) ─ */
    const char *safety_tag = Safety_FaultTag();
    const char *header = (safety_tag[0] != '\0')
                       ? safety_tag
                       : ((m->state <= 3) ? state_names[m->state] : "???");

    /* Font_7x10 is 7px wide → 18 chars fit in 128px width.
     * Pad so state/tag lands right-justified in the yellow strip. */
    snprintf(buf, sizeof(buf), "PMS%*s", 15 - (int)strlen(header), header);
    ssd1306_SetCursor(0, 3);
    ssd1306_WriteString(buf, Font_7x10, White);

    /* ── WHITE AREA: power, efficiency, temperatures ────────────── */

    /* Input power */
    snprintf(buf, sizeof(buf), "Pin  : %5.1f W", m->power_W);
    ssd1306_SetCursor(0, 18);
    ssd1306_WriteString(buf, Font_6x8, White);

    /* Output power */
    float p_out = m->out_voltage_V * m->out_current_A;
    snprintf(buf, sizeof(buf), "Pout : %5.1f W", p_out);
    ssd1306_SetCursor(0, 27);
    ssd1306_WriteString(buf, Font_6x8, White);

    /* Efficiency */
    int eff = 0;
    if (m->power_W > 0.5f) {
        float e = 100.0f * p_out / m->power_W;
        if (e < 0.0f)   e = 0.0f;
        if (e > 150.0f) e = 150.0f;
        eff = (int)e;
    }
    snprintf(buf, sizeof(buf), "Eff  :  %3d %%", eff);
    ssd1306_SetCursor(0, 36);
    ssd1306_WriteString(buf, Font_6x8, White);

    /* Three thermocouples — show "---" if reading is stale or never taken */
    uint32_t now = HAL_GetTick();
    char tc_s[3][4];  /* "-45" or "---" */
    for (uint8_t i = 0; i < 3; i++) {
        bool never = (g_tc_last[i] == 0);
        bool stale = (!never) && (now - g_tc_last[i] > TC_STALE_MS);
        if (never || stale) {
            snprintf(tc_s[i], sizeof(tc_s[i]), "---");
        } else {
            snprintf(tc_s[i], sizeof(tc_s[i]), "%3d", (int)g_tc_C[i]);
        }
    }
    snprintf(buf, sizeof(buf), "T1:%s T2:%s T3:%s",
             tc_s[0], tc_s[1], tc_s[2]);
    ssd1306_SetCursor(0, 45);
    ssd1306_WriteString(buf, Font_6x8, White);

    /* Wiper bottom line */
    snprintf(buf, sizeof(buf), "W:%3u", m->wiper);
    ssd1306_SetCursor(0, 54);
    ssd1306_WriteString(buf, Font_6x8, White);

    ssd1306_UpdateScreen();
}

/**
 * @brief Enable or disable the master boost converter stage.
 *
 * SD_Master  HIGH = enabled (device active)
 * SWEN_Master HIGH = switching enabled
 *
 * Both must be HIGH for the stage to deliver power.
 */
static void boost_master_enable(bool on)
{
    GPIO_PinState state = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(GPIOD, SD_Master_Pin,   state);
    HAL_GPIO_WritePin(GPIOD, SWEN_Master_Pin, state);
}

/**
 * @brief Enable or disable the slave boost converter stage.
 */
static void boost_slave_enable(bool on)
{
    GPIO_PinState state = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(GPIOD, SD_Slave_Pin,   state);
    HAL_GPIO_WritePin(GPIOD, SWEN_Slave_Pin, state);
}

/**
 * @brief Force both boost stages off. Safe default and fault response.
 */
static void boost_disable_all(void)
{
    boost_master_enable(false);
    boost_slave_enable(false);
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

#if DEBUG_I2C_SCAN
  printf("Scanning I2C2...\r\n");
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
      if (HAL_I2C_IsDeviceReady(&hi2c2, (addr << 1), 3, 10) == HAL_OK) {
          printf("  Found device at 0x%02X\r\n", addr);
      }
  }
  printf("Done.\r\n");
#endif

  /* ---- ADS1115 configuration ---- */
  cfg.mux       = ADS1115_MUX_SINGLE_0;
  cfg.pga       = ADS1115_PGA_4_096V;
  cfg.mode      = ADS1115_MODE_SINGLE;
  cfg.dr        = ADS1115_DR_128SPS;
  cfg.comp_mode = ADS1115_COMP_MODE_TRAD;
  cfg.comp_pol  = ADS1115_COMP_POL_LOW;
  cfg.comp_lat  = ADS1115_COMP_LAT_OFF;
  cfg.comp_que  = ADS1115_COMP_QUE_DISABLE;

  /* ---- Boost converters start in OFF state ----
   * Explicit safe default: both master and slave disabled at boot.
   * The safety module owns enable/disable from this point forward;
   * boost_master_enable() is called once below after init, then only
   * toggled in response to safety state transitions in the main loop.
   */
  boost_disable_all();
  HAL_Delay(10);

  /* ---- Start FDCAN2 (non-fatal for debugging) ---- */
  if (HAL_FDCAN_Start(&hfdcan2) != HAL_OK) {
      printf("FDCAN2 start FAILED\r\n");
  } else {
      printf("FDCAN2 started OK\r\n");
  }

  /* ---- Initialize MPPT ---- */
  MPPT_Config_t mppt_cfg = {
      .wiper_min     = 179,
      .wiper_max     = 255,
      .po_step       = 1,
      .scan_step     = 5,
      .scan_interval = MPPT_SCAN_INTERVAL_DEFAULT,
  };
  MPPT_Init(&mppt_cfg);
  printf("MPPT initialized\r\n");

  /* ---- Initialize Safety ---- */
  Safety_Config_t safety_cfg = {
      .vin_max_V      = SAFETY_DEFAULT_VIN_MAX_V,
      .vin_min_V      = SAFETY_DEFAULT_VIN_MIN_V,
      .iin_max_A      = SAFETY_DEFAULT_IIN_MAX_A,
      .vout_max_V     = SAFETY_DEFAULT_VOUT_MAX_V,
      .iout_max_A     = SAFETY_DEFAULT_IOUT_MAX_A,
      .temp_max_C     = SAFETY_DEFAULT_TEMP_MAX_C,
      .tc_stale_ms    = SAFETY_DEFAULT_TC_STALE_MS,
      .bms_timeout_ms = SAFETY_DEFAULT_BMS_TIMEOUT_MS,
      .recovery_ms    = SAFETY_DEFAULT_RECOVERY_MS,
  };
  Safety_Init(&safety_cfg);
  printf("Safety initialized\r\n");

  /* ---- Initialize OLED ---- */
  ssd1306_Init();
  ssd1306_Fill(Black);
  ssd1306_SetCursor(10, 24);
  ssd1306_WriteString("Booting...", Font_11x18, White);
  ssd1306_UpdateScreen();
  HAL_Delay(500);
  printf("OLED initialized\r\n");

  /* ---- All subsystems ready — enable master boost stage ----
   * Main loop will disable the boost if safety reports SHUTDOWN
   * and re-enable it when safety returns to SAFE after recovery.
   */
  boost_master_enable(true);
  printf("Boost master enabled\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
      /* ── Heartbeat LEDs ───────────────────────────────────────── */
      HAL_GPIO_TogglePin(GPIOG, CAN_NMT_STATUS_Pin);
      HAL_GPIO_TogglePin(GPIOG, CAN_ERROR_STATE_Pin);

      /* ── MPPT + ADC + safety check + telemetry (every 50 ms) ──── */
      static uint32_t last_mppt = 0;
      if (HAL_GetTick() - last_mppt >= 50) {
          last_mppt = HAL_GetTick();

          float v_in, i_in, v_out, i_out;
          read_adc(&v_in, &i_in, &v_out, &i_out);

          /* Safety evaluates *before* MPPT so we never drive the pot
           * with a value from a cycle that turned out to be faulted. */
          Safety_Inputs_t si = {
              .v_in          = v_in,
              .i_in          = i_in,
              .v_out         = v_out,
              .i_out         = i_out,
              .tc_C          = g_tc_C,
              .tc_last       = g_tc_last,
              .bms_last_tick = 0,
              .bms_enabled   = false,   /* BMS not wired into main yet */
          };
          Safety_Response_t rsp = Safety_Check(&si);

          if (rsp == SAFETY_SHUTDOWN) {
              /* Kill the boost and skip MPPT. The algorithm state is
               * preserved — when we recover, P&O picks up where it left
               * off rather than starting fresh. */
              boost_disable_all();
          } else {
              /* Re-enable on the first cycle after recovery; cheap
               * GPIO write so calling it every cycle is fine. */
              boost_master_enable(true);

              uint8_t wiper = MPPT_Step(v_in, i_in, v_out, i_out);
              AD5245_SetWiper(&hi2c2, AD5245_ADDR_AD0_LOW, wiper);
          }

          print_telemetry(MPPT_GetData());
      }

      /* ── OLED update (every 500 ms) ───────────────────────────── */
      static uint32_t last_oled = 0;
      if (HAL_GetTick() - last_oled >= 500) {
          last_oled = HAL_GetTick();
          update_oled(MPPT_GetData());
      }

      /* ── Thermocouple reads (every 1 s) ───────────────────────── */
      static uint32_t last_tc = 0;
      if (HAL_GetTick() - last_tc >= 1000) {
          last_tc = HAL_GetTick();
          read_thermocouples();

//    	  /* Keep fake cache fresh so TC_STALE never trips */
//		  uint32_t now = HAL_GetTick();
//		  for (uint8_t i = 0; i < 3; i++) {
//			  g_tc_C[i]    = 25.0f;
//			  g_tc_last[i] = now;
//		  }
      }

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
