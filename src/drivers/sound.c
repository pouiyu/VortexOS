#include "sound.h"
#include <io.h>
#include <vga.h>

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND 0x43
#define PIT_CHANNEL2_DATA 0x42
#define PIT_INPUT_FREQ 1193180

void soundPlay(uint32_t frequency) {
    if (frequency == 0) return;
    
    uint32_t divisor = PIT_INPUT_FREQ / frequency;
    
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2_DATA, divisor & 0xFF);
    outb(PIT_CHANNEL2_DATA, (divisor >> 8) & 0xFF);
    
    uint8_t status = inb(0x61);
    outb(0x61, status | 0x03);
}

void soundStop(void) {
    uint8_t status = inb(0x61) & 0xFC;
    outb(0x61, status);
}

// 使用 PIT 通道 0 延迟（更准确）
static void delay_ms(uint32_t ms) {
    // PIT 频率 1.19318 MHz，每 tick 约 0.838 微秒
    // 1ms = 1193 ticks
    uint32_t ticks = ms * 1193;
    
    // 设置 PIT 通道 0 为单次计数模式
    outb(PIT_COMMAND, 0x30);
    outb(PIT_CHANNEL0_DATA, ticks & 0xFF);
    outb(PIT_CHANNEL0_DATA, (ticks >> 8) & 0xFF);
    
    // 等待计数完成（读取状态位）
    uint8_t status;
    do {
        outb(PIT_COMMAND, 0xE2);  // 读取通道 0 状态
        status = inb(PIT_CHANNEL0_DATA);
    } while (status & 0x80);  // 等待计数值达到 0
}

void soundBeep(uint32_t frequency, uint32_t durationMs) {
    soundPlay(frequency);
    
    // 最短声音至少 50ms
    uint32_t actualMs = durationMs < 50 ? 50 : durationMs;
    
    delay_ms(actualMs);
    soundStop();
}

void soundInit(void) {
    soundStop();
    vgaPutStr("[SOUND] Initialized\n");
}