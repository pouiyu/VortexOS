#include <mouse.h>
#include <io.h>
#include <serial.h>

/* 8042 状态寄存器(端口 0x64, 读)位定义 */
#define STAT_OUT_BUF_FULL  0x01  // bit0: 输出缓冲(0x60)有数据
#define STAT_IN_BUF_FULL   0x02  // bit1: 输入缓冲(0x60)忙
#define STAT_AUX_SIGNAL    0x20  // bit5: 输出缓冲数据来自辅助设备(鼠标)

/* 累积位移与光标状态 */
static volatile int mouseDX = 0;
static volatile int mouseDY = 0;
static volatile int mouseAbsX = 0;
static volatile int mouseAbsY = 0;
static volatile uint8_t mouseButtons = 0;
static volatile int mouseEventPending = 0;

/* 光标活动范围边界(由 mouseSetBounds 设置, 默认 640x480) */
static int mouseBoundW = 640;
static int mouseBoundH = 480;

/* 3 字节包接收状态机 */
static uint8_t packetBuf[MOUSE_PACKET_SIZE];
static int packetIndex = 0;
static bool awaitingSync = true;

/* 等待 8042 输入缓冲可写(status bit1 == 0) */
static void mouseWaitWrite(void) {
    for (int i = 0; i < 10000; i++) {
        if (!(inb(MOUSE_COMMAND_PORT) & STAT_IN_BUF_FULL))
            return;
    }
    serialPutStr("mouse: 8042 write timeout\n");
}

/* 等待 8042 输出缓冲有数据并读取一字节(不校验内容) */
static uint8_t mouseWaitByte(void) {
    for (;;) {
        if (inb(MOUSE_COMMAND_PORT) & STAT_OUT_BUF_FULL)
            return inb(MOUSE_DATA_PORT);
    }
}

/* 等待 8042 输出缓冲有数据且读到鼠标 ACK(0xFA) */
static void mouseWaitRead(void) {
    for (int i = 0; i < 10000; i++) {
        uint8_t status = inb(MOUSE_COMMAND_PORT);
        if (status & STAT_OUT_BUF_FULL) {
            uint8_t data = inb(MOUSE_DATA_PORT);
            /* 忽略键盘(状态 0x64 写入应答)与自检数据, 只期待鼠标 ACK(0xFA) */
            if (data == 0xFA)
                return;
        }
    }
    serialPutStr("mouse: 8042 read timeout\n");
}

/* 向鼠标发送一个命令: 先写 0xD4 指示辅助设备, 再写命令字节 */
static void mouseSendCommand(uint8_t cmd) {
    mouseWaitWrite();
    outb(MOUSE_COMMAND_PORT, MOUSE_CMD_WRITE_AUX); // 0xD4
    mouseWaitWrite();
    outb(MOUSE_DATA_PORT, cmd);
    mouseWaitRead();                               // 等待 0xFA ACK
}

void mouseInit(void) {
    /* 禁用辅助设备, 避免初始化期间产生中断 */
    mouseWaitWrite();
    outb(MOUSE_COMMAND_PORT, 0xA7);

    /* 启用辅助设备 */
    mouseWaitWrite();
    outb(MOUSE_COMMAND_PORT, MOUSE_CMD_ENABLE_AUX); // 0xA8

    /* 启用 8042 的 IRQ 中断: 读 command byte, 置上 bit0(键盘IRQ1)+bit1(鼠标IRQ12),
     * 并保持设备未禁用(bit4/bit5=0), 写回。仅 0xA8 只开数据传输, 不会触发 IRQ12。 */
    mouseWaitWrite();
    outb(MOUSE_COMMAND_PORT, 0x20);                 // 读 command byte
    uint8_t cmdByte = mouseWaitByte();              // 返回数据即为 command byte
    cmdByte |= 0x03;                                // bit0 IRQ1 + bit1 IRQ12
    cmdByte &= (uint8_t)~0x30;                      // bit4/bit5=0: 不禁用设备
    mouseWaitWrite();
    outb(MOUSE_COMMAND_PORT, 0x60);                 // 写 command byte
    mouseWaitWrite();
    outb(MOUSE_DATA_PORT, cmdByte);

    /* 快速清空任何残留输出数据(跳过其值) */
    uint8_t status = inb(MOUSE_COMMAND_PORT);
    if (status & STAT_OUT_BUF_FULL) {
        inb(MOUSE_DATA_PORT);
    }

    /* 设定鼠标默认配置并启用数据上报 */
    mouseSendCommand(MOUSE_DEV_SET_DEFAULTS);   // 0xF6
    mouseSendCommand(MOUSE_DEV_ENABLE_REPORT);  // 0xF4 (开始周期性发送数据)

    mouseDX = 0;
    mouseDY = 0;
    mouseButtons = 0;
    mouseAbsX = mouseBoundW / 2;
    mouseAbsY = mouseBoundH / 2;
    packetIndex = 0;
    awaitingSync = true;
    mouseEventPending = 0;

    serialPutStr("mouse: init ok\n");
    /* 诊断: 初始化后 8042 状态(bit0=输出有数据, bit5=鼠标数据) */
    serialPutStr("mouse init st=");
    serialPutHex8(inb(MOUSE_COMMAND_PORT));
    serialPutStr(" cb=");
    serialPutHex8(cmdByte);
    serialPutStr("\n");
}

void mouseGetMotion(int* dx, int* dy) {
    *dx = mouseDX;
    *dy = mouseDY;
    mouseDX = 0;
    mouseDY = 0;
    mouseEventPending = 0;
}

bool mouseHasEvent(void) {
    return mouseEventPending != 0;
}

uint8_t mouseGetButtons(void) {
    return mouseButtons;
}

int mouseGetX(void) {
    return mouseAbsX;
}

int mouseGetY(void) {
    return mouseAbsY;
}

void mouseSetPosition(int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= mouseBoundW) x = mouseBoundW - 1;
    if (y >= mouseBoundH) y = mouseBoundH - 1;
    mouseAbsX = x;
    mouseAbsY = y;
}

void mouseSetBounds(int w, int h) {
    mouseBoundW = w;
    mouseBoundH = h;
    mouseSetPosition(mouseAbsX, mouseAbsY);
}

void mouseIRQHandler(void) {
    /* 只处理来自辅助设备(鼠标)的数据 */
    if (!(inb(MOUSE_COMMAND_PORT) & STAT_OUT_BUF_FULL)) {
        outb(0xA0, 0x20);        /* EOI(从片) */
        outb(0x20, 0x20);        /* EOI(主片) */
        return;
    }

    /* 一次性读空输出缓冲, 逐字节喂给 3 字节帧状态机, 避免高速移动丢包。
     * 键盘与鼠标共用 0x60 输出缓冲: 只有 bit5(AUX) 才属于鼠标数据, 键盘扫描码
     * 不能在这里消费(会破坏包同步, 且抢走键盘字节), 遇到非鼠标字节立即停下 */
    for (;;) {
        uint8_t st = inb(MOUSE_COMMAND_PORT);
        if (!(st & STAT_OUT_BUF_FULL))
            break;
        if (!(st & STAT_AUX_SIGNAL))
            break;                              /* 缓冲里是键盘数据, 留给 IRQ1 */
        uint8_t data = inb(MOUSE_DATA_PORT);

        /* 帧同步: 帧首字节 bit3 恒为 1 */
        if (awaitingSync) {
            if (!(data & MOUSE_B0_ALWAYS))
                continue;                     /* 未同步, 跳过该字节找帧头 */
            packetIndex = 0;
            awaitingSync = false;
        }

        packetBuf[packetIndex++] = data;

        if (packetIndex == MOUSE_PACKET_SIZE) {
            uint8_t b0 = packetBuf[0];
            int8_t dx = (int8_t)packetBuf[1];
            int8_t dy = (int8_t)packetBuf[2];

            mouseButtons = (b0 & 0x07);       // bit0~2: 左/右/中
            mouseDX += dx;
            mouseDY -= dy;                    // Y 方向按实测取反, 使光标与操作方向一致
            /* 直接用本包增量更新光标绝对位置(而非累积总值), 保证移动速度与
             * 硬件位移一一对应、恒定；累积值另存 mouseDX/DY 供读取方一次性取走 */
            mouseAbsX += dx;
            mouseAbsY += -dy;

            if (mouseAbsX < 0) mouseAbsX = 0;
            if (mouseAbsY < 0) mouseAbsY = 0;
            if (mouseAbsX >= mouseBoundW) mouseAbsX = mouseBoundW - 1;
            if (mouseAbsY >= mouseBoundH) mouseAbsY = mouseBoundH - 1;

            mouseEventPending = 1;
            packetIndex = 0;
            awaitingSync = true;
        }
    }

    outb(0xA0, 0x20);        /* EOI(从片) */
    outb(0x20, 0x20);        /* EOI(主片) */
}