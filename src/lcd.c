#include "stm32f0xx_gpio.h"
#include "stm32f0xx_rcc.h"
#include "stm32f0xx_spi.h"
#include "cmsis/cmsis_device.h"
#include "diag/Trace.h"
#include "lcd.h"

// ----------------------------------------------------------------------------
//                      DEFINES
// ----------------------------------------------------------------------------

#ifndef VERBOSE
#define VERBOSE 0
#endif

// Maps to EN and RS bits for LCD
#define LCD_ENABLE (0x80)
#define LCD_DISABLE (0x0)
#define LCD_COMMAND (0x0)
#define LCD_DATA (0x40)

// LCD Config commands
#define LCD_CURSOR_ON (0x2)
#define LCD_MOVE_CURSOR_CMD (0x80)
#define LCD_CLEAR_CMD (0x1)
#define LCD_FIRST_ROW_OFFSET (0x0)
#define LCD_SECOND_ROW_OFFSET (0x40)

// Positions on the LCD character table
#define CHAR_OMEGA (0xF4)

#define LEADING_ZERO_FLAG (69)
#define DIGITS_TO_WRITE (5)
#define DELAY_PRESCALER_1KHZ (47999) /* 48 MHz / (47999 + 1) = 1 kHz */
#define DELAY_PERIOD_DEFAULT (100) /* 100 ms */

// ----------------------------------------------------------------------------
//                      IMPLEMENTATION
// ----------------------------------------------------------------------------

void LCD_Init(void){
    // Configure GPIOB to control LCD via SPI
    myGPIOB_Init();
    mySPI_Init();

    // We'll need the delay timer to wait for some operations (clear) to
    // complete on the LCD. The sane way to do this is to read the LCD status
    // but the PBMCUSLK hard-wires the LCD to write mode :(
    DELAY_Init();

    // Configure the internal LCD controls
    //
    // PBMCUSLK has a shift register connected to the LCD like:
    //
    //    ______________HC 595______________
    //    | Q7  Q6  Q5  Q4  Q3  Q2  Q1  Q0 |  0   0   0   0
    //    |=|===|===|===|===|===|===|===|==‾‾‾|‾‾‾|‾‾‾|‾‾‾|‾‾|
    //    | EN  RS  NC  NC  D7  D6  D5  D4    D3  D2  D1  D0 |
    //    ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾HD44780 LCD Controller‾‾‾‾‾‾‾‾‾‾‾‾‾‾
    //
    // On initialization, the LCD may be in 4b or 8b mode. We need to change it
    // to 4b mode to use LCD_SendWord, which assumes 4b mode and sends high and
    // low half-words.
    //
    // If the LCD is in 8b mode:
    //   1. Send 0x0, interpreted as command 0x00 which has no effect
    //   2. Send 0x2, interpreted as command 0x20 which changes to 4b mode
    //
    // If the LCD is in 4b mode:
    //   1. Send 0x0, interpreted as high half-word
    //   2. Send 0x2, interpreted as lower half-word, command 0x02 is "send
    //      cursor home"
    // Since we entered in 4b mode we can continue with the rest of the
    // initialization.
    // Note, "cursor home" has a 1.52 ms execution time so we add a 2ms delay to
    // ensure the LCD is ready to receive the rest of the config.
    LCD_SendWord(LCD_COMMAND, 0x2);
    DELAY_Set(2);

    // Now we're in 4b mode we can do the rest of the LCD config
    // https://en.wikipedia.org/wiki/Hitachi_HD44780_LCD_controller#Instruction_set
    // 1. set 4b, 2 line, 5x7 font
    // 2. display on, blink off, cursor?
    // 3. cursor auto-increment, no display shift
    uint8_t showCursorState = VERBOSE ? LCD_CURSOR_ON : 0x0;
    LCD_SendWord(LCD_COMMAND, 0x28);
    LCD_SendWord(LCD_COMMAND, 0x0C | showCursorState);
    LCD_SendWord(LCD_COMMAND, 0x06);
    LCD_Clear();

    // Write the initial units and resistance / freq placeholders manually
    //   "  ???  H"
    //   "  ???  Ω"
    LCD_MoveCursor(LCD_FREQ_ROW, 3);
    LCD_SendText("???");
    LCD_MoveCursor(LCD_FREQ_ROW, 8);
    LCD_SendASCIIChar("H");

    LCD_MoveCursor(LCD_RESISTANCE_ROW, 3);
    LCD_SendText("???");
    LCD_MoveCursor(LCD_RESISTANCE_ROW, 8);
    LCD_SendWord(LCD_DATA, CHAR_OMEGA);
}

void myGPIOB_Init(void){
    // Turn on the GPIOB clock
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);

    // PB3 --AF0-> SPI MOSI
    // PB5 --AF0-> SPI SCK
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_3 | GPIO_Pin_5;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Configure the LCK pin for "manual" control in HC595_Write
    GPIO_InitStruct.GPIO_Pin   = LCD_LCK_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void mySPI_Init(void){
    // Turn on the SPI clock
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

    SPI_InitTypeDef SPI_InitStruct;
    SPI_InitStruct.SPI_Direction     = SPI_Direction_1Line_Tx;
    SPI_InitStruct.SPI_Mode          = SPI_Mode_Master;
    SPI_InitStruct.SPI_DataSize      = SPI_DataSize_8b;
    SPI_InitStruct.SPI_CPOL          = SPI_CPOL_Low;
    SPI_InitStruct.SPI_CPHA          = SPI_CPHA_1Edge;
    SPI_InitStruct.SPI_NSS           = SPI_NSS_Soft;
    SPI_InitStruct.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
    SPI_InitStruct.SPI_FirstBit      = SPI_FirstBit_MSB;
    SPI_InitStruct.SPI_CRCPolynomial = 7;

    SPI_Init(SPI1, &SPI_InitStruct);
    SPI_Cmd(SPI1, ENABLE);
}

void HC595_Write(uint8_t word){
    // We only want to expose the register values to the LCD once the new word
    // has loaded completely. We do this by toggling LCK, which controls whether
    // or not the register output tracks its current contents

    // Don't update the register output; LCK = 0
    GPIOB->BRR = LCD_LCK_PIN;

    // Poll SPI until its ready to receive more data
    while (!SPI_ReadyToSend()) {
        /* polling... */
    };

    SPI_SendData8(SPI1, word);

    // Poll SPI to determine when it's finished transmitting
    while (!SPI_DoneSending()) {
        /* polling... */
    };

    // Update the output; LCK = 1
    GPIOB->BSRR = LCD_LCK_PIN;
}

uint8_t SPI_ReadyToSend(void){
    // SPI can accept more data into its TX queue if TXE = 1 OR BSY = 0
    return ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == SET)
            || (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == RESET));
}

uint8_t SPI_DoneSending(void){
    // SPI is done sending when BSY = 0
    return (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == RESET);
}

void LCD_SendWord(uint8_t type, uint8_t word){
    // The high half of the register output is always reserved for EN and RS.
    // To send an 8b target word we have to send it as two sequential 4b
    // half-words, with the target half-words in the lower half of the word.

    uint8_t high = ((word & 0xF0) >> 4);
    uint8_t low = word & 0x0F;

    HC595_Write(LCD_DISABLE | type | high);
    HC595_Write(LCD_ENABLE  | type | high);
    HC595_Write(LCD_DISABLE | type | high);

    HC595_Write(LCD_DISABLE | type | low);
    HC595_Write(LCD_ENABLE  | type | low);
    HC595_Write(LCD_DISABLE | type | low);
}

void LCD_SendASCIIChar(const char* character){
    LCD_SendWord(LCD_DATA, (uint8_t)(*character));
}

void LCD_SendText(char* text){
    const char* ch = text;
    while(*ch){
        LCD_SendASCIIChar(ch++);
    }
}

void LCD_SendDigit(uint8_t digit){
    // Enforce range of 0:9
    uint8_t safeDigit = 0;
    if (digit > 9){
        safeDigit = 9;
    } else {
        safeDigit = digit;
    }

    // Digits on the ASCII table are mapped like:
    //   0x30 -> 0
    //   0x31 -> 1
    //   ...
    //   0x39 -> 9
    uint8_t asciiDigit = 0x30 + safeDigit;

    LCD_SendWord(LCD_DATA, asciiDigit);
}

void LCD_MoveCursor(uint8_t row, uint8_t col){
    // We only have 2 rows so cap the selected row at 2
    uint8_t rowOffset = 0x0;
    if (row <= 1) {
        rowOffset = LCD_FIRST_ROW_OFFSET;
    } else {
        rowOffset = LCD_SECOND_ROW_OFFSET;
    }

    // Similarly, constrain allowed column input values to 1:8
    // and then shift for 0-indexing on the LCD
    uint8_t colOffset = 0x0;
    if (col == 0) {
        colOffset = 0x0;
    } else if (col > 0 && col < 8) {
        colOffset = col - 1;
    } else {
        // Constrain >8 to last column
        colOffset = 0x7;
    }

    uint8_t moveCursorCommand = LCD_MOVE_CURSOR_CMD | rowOffset | colOffset;

    LCD_SendWord(LCD_COMMAND, moveCursorCommand);
}

void LCD_Clear(void){
    LCD_SendWord(LCD_COMMAND, LCD_CLEAR_CMD);
    DELAY_Set(2);
}

void DELAY_Init()
{
    // Enable timer clock
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    // Set timer to:
    //   Auto reload buffer
    //   Stop on overflow
    //   Enable update events
    //   Interrupt on overflow only
    TIM3->CR1 = ((uint16_t) 0x8C);

    TIM3->PSC = DELAY_PRESCALER_1KHZ;
    TIM3->ARR = DELAY_PERIOD_DEFAULT;

    // Update timer registers.
    TIM3->EGR |= 0x0001;
}

void DELAY_Set(uint32_t milliseconds){
    // Clear timer
    TIM3->CNT |= 0x0;

    // Set timeout
    TIM3->ARR = milliseconds;

    // Update timer registers
    TIM3->EGR |= 0x0001;

    // Start the timer
    TIM3->CR1 |= TIM_CR1_CEN;

    // Loop until interrupt flag is set by timer expiry
    while (!(TIM3->SR & TIM_SR_UIF)) {
        /* polling... */
    }

    // Stop the timer
    TIM3 -> CR1 &= ~(TIM_CR1_CEN);

    // Reset the interrupt flag
    TIM3->SR &= ~(TIM_SR_UIF);
}

void LCD_UpdateFreq(float freq){
    if (freq < 1) {
        LCD_MoveCursor(LCD_FREQ_ROW, 1);
        LCD_SendText("<    1");
    } else if (freq > 99999) {
        LCD_MoveCursor(LCD_FREQ_ROW, 1);
        LCD_SendText(">99999");
    } else {
        LCD_UpdateRow(LCD_FREQ_ROW, freq);
    }
}

void LCD_UpdateResistance(float resistance){
    if (resistance < 1) {
        LCD_MoveCursor(LCD_RESISTANCE_ROW, 1);
        LCD_SendText("<    1");
    } else {
        LCD_UpdateRow(LCD_RESISTANCE_ROW, resistance);
    }
}

void LCD_UpdateRow(uint8_t row, float val){
    // We have five characters to write to (8 less one for > or <, one for
    // metric scale, and one for unit symbol) and we'll be displaying ints so
    // our display range is 1 -> 99999.
    // We also want to avoid displaying leading 0s.

    uint32_t intVal = (uint32_t)val;

    uint8_t ones = intVal % 10;
    uint8_t tens = (intVal / 10) % 10;
    uint8_t hundreds = (intVal / 100) % 10;
    uint8_t thousands = (intVal / 1000) % 10;
    uint8_t tenThousands = (intVal / 10000) % 10;

    uint8_t orderedDigits[DIGITS_TO_WRITE] = {
            tenThousands,
            thousands,
            hundreds,
            tens,
            ones
    };

    // Flag leading zeroes with number outside [0-9] range
    for (int i = 0; i < DIGITS_TO_WRITE; i++) {
        if (orderedDigits[i] != 0) {
            // Stop searching after we find the first non-zero number.
            // Since range is >= 1 this is guaranteed to occur
            break;
        } else {
            orderedDigits[i] = LEADING_ZERO_FLAG;
        }
    }

    // Print the values to the LCD
    LCD_MoveCursor(row, 1);
    LCD_SendASCIIChar(" "); // overwrite over/under range flag
    for (int i = 0; i < DIGITS_TO_WRITE; i++) {
        if (orderedDigits[i] == LEADING_ZERO_FLAG) {
            LCD_SendASCIIChar(" ");
        } else {
            LCD_SendDigit(orderedDigits[i]);
        }
    }
}
