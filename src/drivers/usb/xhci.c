#include "xhci.h"
#include "pci.h"
#include <interruption/idt.h>
#include <mm/pmm.h>
#include <mm/paging.h>
#include <string/string.h>
#include <io.h>
#include <serial.h>
#include <stdio/vga.h>

/* ============================ 寄存器位定义 ============================ */
#define CMD_RS          (1u<<0)
#define CMD_HCRST       (1u<<1)
#define CMD_INTE        (1u<<2)

#define STS_HCH         (1u<<0)
#define STS_HCE         (1u<<2)
#define STS_EINT        (1u<<3)
#define STS_PCD         (1u<<4)

/* ============================ TRB 定义 ============================ */
typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
    uint32_t dw3;
} __attribute__((packed)) XhTrb;

#define TRB_TYPE_NORMAL         1
#define TRB_TYPE_SETUP          2
#define TRB_TYPE_DATA           3
#define TRB_TYPE_STATUS         4
#define TRB_TYPE_LINK           6
#define TRB_TYPE_ENABLE_SLOT    9
#define TRB_TYPE_DISABLE_SLOT   10
#define TRB_TYPE_ADDRESS_DEVICE 11
#define TRB_TYPE_CONFIGURE_EP   12

#define TRB_TYPE_TRANSFER_EVENT    32
#define TRB_TYPE_COMMAND_COMPLETION 33
#define TRB_TYPE_PORT_STATUS_CHANGE 34

#define TRB_C           (1u<<0)
#define TRB_TYPE_SHIFT  10
#define TRB_LINK_TC     (1u<<1)

#define CC_SUCCESS      1

#define DATA_DIR_IN     (1u<<16)    /* Data TRB dw3 方向位 */
#define STATUS_DIR_IN   (1u<<0)     /* Status TRB dw2 方向位 */
#define STATUS_IOC      (1u<<16)    /* Status TRB dw2 IOC */
#define NORMAL_IOC      (1u<<31)    /* Normal TRB dw2 IOC */

#define RING_SIZE       256         /* 段内 TRB 总数，索引 255 为 Link */

/* 运行时寄存器区：offset 0x00=MFINDEX，interrupter n 从 0x20 + n*0x20 开始 */
#define RT_INTR0        (0x20)      /* interrupter 0 基偏移 */
#define RT_IMAN(o)      (RT_INTR0 + (o) + 0x00)   /* 相对 interrupter0 的 IMAN */
#define RT_IMOD(o)      (RT_INTR0 + (o) + 0x04)
#define RT_ERSTSZ(o)    (RT_INTR0 + (o) + 0x08)
#define RT_ERSTBA(o)    (RT_INTR0 + (o) + 0x10)
#define RT_ERDP(o)      (RT_INTR0 + (o) + 0x18)

#define MAX_SLOTS       8

/* ============================ 状态结构 ============================ */
typedef struct {
    volatile XhTrb* trbs;
    uint32_t enq;
    int cycle;
} XhTrRing;

typedef struct {
    uint8_t  slotId;
    bool     inUse;
    uint8_t  port;
    uint8_t  speed;
    uint8_t  ep0Max;
    uint8_t  deviceClass, deviceSubclass, deviceProtocol;
    bool     hasEp1In;
    uint16_t ep1MaxPacket;
    uint8_t  ep1Interval;
    XhTrRing ep0Ring;
    XhTrRing ep1Ring;
    uint8_t* ep0Buf;
    uint8_t* ep1Buf;
    XhciReportHandler reportHandler;
} XhciSlot;

static pciDevice   gPci;
static uintptr_t   gBase = 0;
static uintptr_t   gOpBase;
static uintptr_t   gDbBase;
static uintptr_t   gRtBase;
static uint8_t     gMaxSlots;
static uint8_t      gMaxPorts;
static uint8_t      gCsz;            /* CSZ：1 = 64 字节上下文，0 = 32 */
static uint32_t     gContextSize;

static void*    gDcbaa;
static volatile XhTrb* gCmdRing;
static volatile XhTrb* gEventSeg;
static volatile XhTrb* gErst;
static uintptr_t   gCmdRingPhys;

static void*    gCtxPool[MAX_SLOTS];
static void*    gInCtxPool[MAX_SLOTS];
static uint8_t* gEp0BufPool[MAX_SLOTS];
static uint8_t* gEp1BufPool[MAX_SLOTS];
static void*    gRingPool[MAX_SLOTS][2];
static XhciSlot gSlots[MAX_SLOTS];

static uint32_t gEventDeq = 0;
static int      gEventCycle = 1;
static int      gCmdCycle = 1;
static uint32_t gCmdEnq = 0;

static volatile bool     gCmdDone;
static volatile uint32_t gCmdCode;
static volatile uint32_t gCmdSlotId;
static volatile bool     gXferDone;
static volatile uint32_t gXferCode;
static volatile uint32_t gXferSlot;
static volatile uint32_t gXferEp;
static volatile uint32_t gXferLen;

/* ============================ MMIO ============================ */
static inline void xw32(uintptr_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(base + off) = v;
}
static inline uint32_t xr32(uintptr_t base, uint32_t off) {
    return *(volatile uint32_t*)(base + off);
}

/* ============================ 传输环 ============================ */
/* 预填 Link TRB；正常使用不会触达（每次只使用极少数 TRB） */
static void ringReset(XhTrRing* r, uintptr_t phys) {
    memset((void*)phys, 0, RING_SIZE * (uint32_t)sizeof(XhTrb));
    r->trbs = (volatile XhTrb*)phys;
    r->enq = 0;
    r->cycle = 1;
    volatile XhTrb* link = &r->trbs[RING_SIZE - 1];
    link->dw0 = (uint32_t)phys;
    link->dw1 = (uint32_t)(phys >> 32);
    link->dw2 = 0;
    link->dw3 = TRB_C | TRB_LINK_TC | (TRB_TYPE_LINK << TRB_TYPE_SHIFT);
}

static void ringPut(XhTrRing* r, const XhTrb* t) {
    volatile XhTrb* slot = &r->trbs[r->enq];
    slot->dw0 = t->dw0;
    slot->dw1 = t->dw1;
    slot->dw2 = t->dw2;
    slot->dw3 = (t->dw3 & ~TRB_C) | (r->cycle ? TRB_C : 0);
    r->enq++;
    if (r->enq >= RING_SIZE - 1) r->enq = 0;   /* 避免踩到 Link（正常情况下不会发生） */
}

/* ============================ 事件环 ============================ */
static int processOneEvent(void) {
    volatile XhTrb* e = &gEventSeg[gEventDeq % RING_SIZE];
    int cycle = (e->dw3 & TRB_C) ? 1 : 0;
    if (cycle != gEventCycle) return -1;

    uint32_t type = (e->dw3 >> TRB_TYPE_SHIFT) & 0x3F;
    if (type == TRB_TYPE_COMMAND_COMPLETION) {
        gCmdCode = (e->dw2 >> 24) & 0xFF;
        gCmdSlotId = (e->dw3 >> 24) & 0xFF;
        gCmdDone = true;
    } else if (type == TRB_TYPE_TRANSFER_EVENT) {
        gXferCode = (e->dw2 >> 24) & 0xFF;
        gXferLen  = e->dw2 & 0xFFFFFF;
        gXferSlot = (e->dw3 >> 24) & 0xFF;
        gXferEp   = (e->dw3 >> 16) & 0xFF;
        gXferDone = true;
    }

    gEventDeq++;
    if (gEventDeq % RING_SIZE == 0) gEventCycle ^= 1;
    return (int)type;
}

static void updateErdp(void) {
    uintptr_t addr = (uintptr_t)(&gEventSeg[gEventDeq % RING_SIZE]);
    xw32(gRtBase, RT_ERDP(0), (uint32_t)addr);
    xw32(gRtBase, RT_ERDP(0) + 4, 0);
}

static void ringDoorbell(uint8_t slotId, uint32_t target) {
    xw32(gDbBase, slotId * 4, target & 0xFF);
}

static void putCommand(const XhTrb* t) {
    volatile XhTrb* slot = &gCmdRing[gCmdEnq];
    slot->dw0 = t->dw0;
    slot->dw1 = t->dw1;
    slot->dw2 = t->dw2;
    slot->dw3 = (t->dw3 & ~TRB_C) | (gCmdCycle ? TRB_C : 0);
    gCmdEnq++;
    if (gCmdEnq >= RING_SIZE - 1) {
        gCmdEnq = 0;
        gCmdCycle ^= 1;
    }
    ringDoorbell(0, 0);
}

static int waitForEventType(uint32_t wantType, int timeout) {
    while (timeout-- > 0) {
        if (wantType == TRB_TYPE_COMMAND_COMPLETION && gCmdDone) return 0;
        if (wantType == TRB_TYPE_TRANSFER_EVENT && gXferDone) return 0;
        int t = processOneEvent();
        if (t == (int)wantType) return 0;
        if (t == -1) {
            for (volatile int i = 0; i < 100; i++);
        }
    }
    return -1;
}

/* ============================ 上下文辅助 ============================ */
/* Input Control Context 固定占 32 字节（dword0=Add flags, dword1=Drop flags，
   dword2..7 保留），因此 Slot Context 从 +32 起，EP Context 从 +32+n*ctxSize 起 */
static inline uint32_t* slotCtx(void* inp)  { return (uint32_t*)((uint8_t*)inp + 32); }
static inline uint32_t* epCtx(void* inp, uint32_t dci) {
    return (uint32_t*)((uint8_t*)inp + 32 + gContextSize * dci);
}

/* ============================ 控制传输（EP0） ============================ */
static int doControl(XhciSlot* s, uint8_t bmReqType, uint8_t bReq, uint16_t wValue,
                     uint16_t wIndex, uint16_t wLength, void* data, bool dirIn) {
    uint8_t setup[8];
    setup[0] = bmReqType;
    setup[1] = bReq;
    setup[2] = (uint8_t)(wValue & 0xFF);
    setup[3] = (uint8_t)((wValue >> 8) & 0xFF);
    setup[4] = (uint8_t)(wIndex & 0xFF);
    setup[5] = (uint8_t)((wIndex >> 8) & 0xFF);
    setup[6] = (uint8_t)(wLength & 0xFF);
    setup[7] = (uint8_t)((wLength >> 8) & 0xFF);

    XhTrb t;
    /* Slot ID=[[dw3 31:24]]，EP ID=[[dw3 20:16]]（EP0 = 1） */
    uint32_t se = ((uint32_t)s->slotId << 24) | (1u << 16);
    /* Setup */
    memset(&t, 0, sizeof(t));
    t.dw0 = *(uint32_t*)&setup[0];
    t.dw1 = *(uint32_t*)&setup[4];
    t.dw2 = 8;
    t.dw3 = (TRB_TYPE_SETUP << TRB_TYPE_SHIFT) | se;
    ringPut(&s->ep0Ring, &t);

    /* Data */
    if (wLength > 0 && data) {
        memset(&t, 0, sizeof(t));
        t.dw0 = (uint32_t)(uintptr_t)data;
        t.dw1 = 0;
        t.dw2 = wLength;
        t.dw3 = (TRB_TYPE_DATA << TRB_TYPE_SHIFT) | se;
        if (dirIn) t.dw3 |= DATA_DIR_IN;
        ringPut(&s->ep0Ring, &t);
    }

    /* Status（IOC 置位以产生完成事件） */
    memset(&t, 0, sizeof(t));
    t.dw2 = STATUS_IOC;
    if (wLength > 0 && !dirIn) t.dw2 |= STATUS_DIR_IN;
    t.dw3 = (TRB_TYPE_STATUS << TRB_TYPE_SHIFT) | se;
    ringPut(&s->ep0Ring, &t);

    gXferDone = false;
    ringDoorbell(s->slotId, 1);
    if (waitForEventType(TRB_TYPE_TRANSFER_EVENT, 3000000) != 0) return -1;
    return (gXferCode == CC_SUCCESS) ? 0 : -1;
}

/* ============================ 枚举 ============================ */
static int enableSlot(XhciSlot* s, uint8_t port) {
    XhTrb t;
    memset(&t, 0, sizeof(t));
    t.dw3 = (TRB_TYPE_ENABLE_SLOT << TRB_TYPE_SHIFT);
    gCmdDone = false;
    gCmdCode = 0;
    gCmdSlotId = 0;
    putCommand(&t);
    if (waitForEventType(TRB_TYPE_COMMAND_COMPLETION, 3000000) != 0) {
        serialPutStr("[XHCI] enable-slot timeout (ERDP=");
        serialPutHex32((uint32_t)xr32(gRtBase, RT_ERDP(0)));
        serialPutStr(" CRCR=");
        serialPutHex32((uint32_t)xr32(gOpBase, 0x18));
        serialPutStr(" cmd[0]=");
        serialPutHex32(gCmdRing[0].dw3);
        serialPutStr(")\n");
        return -1;
    }
    serialPutStr("[XHCI] enable-slot code=");
    serialPutHex8((uint8_t)gCmdCode);
    serialPutStr(" slot=");
    serialPutHex8((uint8_t)gCmdSlotId);
    serialPutStr("\n");
    if (gCmdCode != CC_SUCCESS || gCmdSlotId == 0) return -1;

    s->slotId = (uint8_t)gCmdSlotId;
    s->port = port;
    s->inUse = true;
    s->reportHandler = NULL;
    /* DCBAA 指向该 slot 的 device context */
    *(volatile uint64_t*)((uintptr_t)gDcbaa + (uint64_t)s->slotId * 8) =
        (uint64_t)(uintptr_t)gCtxPool[s->slotId];
    return 0;
}

static int addressDevice(XhciSlot* s, bool bsr, uint16_t ep0Max) {
    void* ic = gInCtxPool[s->slotId];
    memset(ic, 0, 0x20 + gContextSize * 4);   /* 32 字节控制区 + 槽/EP 上下文 */
    /* Input Control Context：规范 Add= dword0、Drop= dword1，但 QEMU v8.2.2
       反转了二者（Add 在 dword1、Drop 在 dword0），按 QEMU 布局：Add slot+ep0 */
    ((uint32_t*)ic)[1] = (1u<<0) | (1u<<1);

    uint32_t* sc = slotCtx(ic);
    /* Slot Context（xHCI 规范 6.2.2）：
       DWord0: bits31:27=Context Entries(=1，仅 EP0)，bits26:0=Route String(=0，直连 root hub)
       DWord1: bits23:16=Root Hub Port Number
      （Speed 由 HC 从端口推导，不写入；Slot State 只读，亦不写） */
    sc[0] = (1u << 27);                     /* Context Entries = 1 */
    sc[1] = (uint32_t)s->port << 16;        /* Root Hub Port Number */

    uint32_t* ec = epCtx(ic, 1);
    /* Endpoint Context（规范 6.2.3）：DWord0 bits23:16=Interval，DWord1 bits5:3=EP Type、
       bits31:16=MaxPacketSize，DWord2=TR Dequeue 低32+CCS(bit0)，DWord3=高32 */
    ec[0] = 0;                                              /* EP State=0（HC 会置 RUNNING） */
    ec[1] = (4u << 3) | ((uint32_t)ep0Max << 16);          /* EP Type=Control，MaxPacketSize */
    uintptr_t ringPhys = (uintptr_t)s->ep0Ring.trbs;
    ec[2] = (uint32_t)(ringPhys & 0xFFFFFFF0) | 1;        /* TR Dequeue + DCS */
    ec[3] = (uint32_t)((ringPhys >> 32) & 0xF);

    XhTrb t;
    memset(&t, 0, sizeof(t));
    t.dw0 = (uint32_t)(uintptr_t)ic;
    /* QEMU(XHCITRB) 在 dw3(control) 取 bits31:24 Slot ID、bit9 BSR */
    t.dw3 = (TRB_TYPE_ADDRESS_DEVICE << TRB_TYPE_SHIFT)
          | ((uint32_t)s->slotId << 24)
          | (bsr ? (1u << 9) : 0);
    gCmdDone = false;
    putCommand(&t);
    if (waitForEventType(TRB_TYPE_COMMAND_COMPLETION, 3000000) != 0) {
        serialPutStr("[XHCI] address-device timeout (slot=");
        serialPutHex8(s->slotId);
        serialPutStr(" bsr=");
        serialPutHex8((uint8_t)bsr);
        serialPutStr(" CSZ=");
        serialPutHex8(gCsz);
        serialPutStr(")\n");
        return -1;
    }
    if (gCmdCode != CC_SUCCESS) {
        serialPutStr("[XHCI] address-device code=");
        serialPutHex8((uint8_t)gCmdCode);
        serialPutStr(" slotID=");
        serialPutHex8((uint8_t)gCmdSlotId);
        serialPutStr(" ic=");
        serialPutHex32(*(volatile uint32_t*)ic);
        serialPutStr(" addfl=");
        serialPutHex32(((uint32_t*)ic)[1]);
        serialPutStr(" dcbaa=");
        serialPutHex32(*(volatile uint32_t*)((uintptr_t)gDcbaa + (uintptr_t)s->slotId * 8));
        serialPutStr(" devctx0=");
        serialPutHex32(*(volatile uint32_t*)((uintptr_t)gCtxPool[s->slotId]));
        serialPutStr(" sc0=");
        serialPutHex32(slotCtx(ic)[0]);
        serialPutStr(" ec0=");
        serialPutHex32(epCtx(ic, 1)[0]);
        serialPutStr(" ec2=");
        serialPutHex32(epCtx(ic, 1)[2]);
        serialPutStr("\n");
    }
    return (gCmdCode == CC_SUCCESS) ? 0 : -1;
}

/* 解析配置描述符，找到 interface0 的中断 IN 端点 */
static int configureDevice(XhciSlot* s, uint8_t* cfg, uint32_t cfgLen,
                           uint8_t* ep1Addr, uint16_t* ep1Max, uint8_t* ep1Int) {
    uint8_t* p = cfg;
    while (p + 2 <= cfg + cfgLen) {
        uint8_t len = p[0];
        uint8_t typ = p[1];
        if (len == 0) break;
        if (typ == 4 && len >= 9) {   /* interface */
            s->deviceClass     = p[5];
            s->deviceSubclass  = p[6];
            s->deviceProtocol  = p[7];
        } else if (typ == 5 && len >= 7) {  /* endpoint */
            uint8_t   attr  = p[3];
            uint16_t  wMax  = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
            if ((attr & 0x03) == 0x03 && (p[2] & 0x80)) {   /* interrupt IN */
                *ep1Addr = p[2];
                *ep1Max  = wMax;
                *ep1Int  = p[6];
                return 0;
            }
        }
        p += len;
    }
    return -1;
}

static int configureEndpointIn(XhciSlot* s) {
    void* ic = gInCtxPool[s->slotId];
    memset(ic, 0, 0x20 + gContextSize * 4);

    /* 保留 slot 上下文（之前 Address 建立的） */
    uint32_t dci = 3;   /* EP1 IN */
    /* QEMU 反转布局：Add flags 在 dword1；Add slot(bit0) + EP1 IN(bit DCI) */
    ((uint32_t*)ic)[1] = (1u<<0) | (1u<<dci);

    uint32_t* sc = slotCtx(ic);
    sc[0] = ((uint32_t*)gCtxPool[s->slotId])[0];                     /* 复制现有 slot ctx Dword0（route string 等） */
    sc[0] = (sc[0] & ~(0x1Fu << 27)) | ((uint32_t)dci << 27);        /* Context Entries=高至 DCI，bits31:27 */

    uint32_t* ec = epCtx(ic, dci);
    /* Endpoint Context：DWord0 bits23:16=Interval，DWord1 bits5:3=EP Type、bits31:16=MaxPacketSize */
    ec[0] = (uint32_t)(s->ep1Interval & 0xFF) << 16;       /* Interval */
    ec[1] = (7u << 3)                                       /* EP Type = Interrupt IN */
          | ((uint32_t)s->ep1MaxPacket << 16);             /* MaxPacketSize */
    uintptr_t ringPhys = (uintptr_t)s->ep1Ring.trbs;
    ec[2] = (uint32_t)(ringPhys & 0xFFFFFFF0) | 1;        /* TR Dequeue + DCS */
    ec[3] = (uint32_t)((ringPhys >> 32) & 0xF);

    XhTrb t;
    memset(&t, 0, sizeof(t));
    t.dw0 = (uint32_t)(uintptr_t)ic;
    t.dw3 = (TRB_TYPE_CONFIGURE_EP << TRB_TYPE_SHIFT) | ((uint32_t)s->slotId << 24); /* Slot ID 在 dw3 bits31:24 */
    gCmdDone = false;
    putCommand(&t);
    if (waitForEventType(TRB_TYPE_COMMAND_COMPLETION, 3000000) != 0) return -1;
    return (gCmdCode == CC_SUCCESS) ? 0 : -1;
}

static int enumeratePort(uint8_t port, uint8_t speed) {
    static uint8_t alloc = 0;
    if (alloc >= MAX_SLOTS) return -1;

    const char* stage = "alloc";
    XhciSlot tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.port = port;
    tmp.speed = speed;
    tmp.ep0Buf = gEp0BufPool[alloc];
    tmp.ep1Buf = gEp1BufPool[alloc];
    ringReset(&tmp.ep0Ring, (uintptr_t)gRingPool[alloc][0]);
    ringReset(&tmp.ep1Ring, (uintptr_t)gRingPool[alloc][1]);

    stage = "enable-slot";
    if (enableSlot(&tmp, port) != 0) goto fail;
    XhciSlot* s = &gSlots[tmp.slotId];
    *s = tmp;
    s->inUse = true;
    alloc++;

    /* Phase A：Block Set Address */
    stage = "bsr";
    if (addressDevice(s, true, 8) != 0) goto fail;
    /* 读设备描述符（18 字节）确定 EP0 maxpacket */
    stage = "get-devs-desc";
    if (doControl(s, 0x80, 6, 0x0100, 0, 18, s->ep0Buf, true) != 0) goto fail;
    s->ep0Max = s->ep0Buf[7];
    /* 读配置描述符头部（9 字节）拿 totalLength */
    stage = "get-cfg-head";
    if (doControl(s, 0x80, 6, 0x0200, 0, 9, s->ep1Buf, true) != 0) goto fail;
    uint16_t total = (uint16_t)s->ep1Buf[2] | ((uint16_t)s->ep1Buf[3] << 8);
    /* 读完整配置描述符 */
    if (total > 1024) total = 1024;
    stage = "get-cfg-full";
    if (doControl(s, 0x80, 6, 0x0200, 0, total, s->ep1Buf, true) != 0) goto fail;
    /* Phase B：指派地址 */
    stage = "assign-addr";
    if (addressDevice(s, false, s->ep0Max) != 0) goto fail;

    uint8_t ep1Addr, ep1Int;
    uint16_t ep1Max;
    stage = "parse-cfg";
    if (configureDevice(s, s->ep1Buf, total, &ep1Addr, &ep1Max, &ep1Int) != 0) goto fail;
    s->hasEp1In = true;
    s->ep1MaxPacket = ep1Max;
    s->ep1Interval = ep1Int;

    /* 设置配置 & 启动 Boot Protocol（HID） */
    uint8_t cfgVal = s->ep1Buf[5];
    stage = "set-config";
    if (doControl(s, 0x00, 9, cfgVal, 0, 0, NULL, false) != 0) goto fail;
    if (s->deviceClass == 3 && s->ep0Max)
        doControl(s, 0x21, 11, 1, 0, 0, NULL, true);  /* SET_PROTOCOL Boot */

    stage = "conf-ep";
    if (configureEndpointIn(s) != 0) goto fail;

    return 0;
fail:
    serialPutStr("[USB] device init failed @ ");
    serialPutStr(stage);
    serialPutStr("\n");
    vgaPutStr("[USB] device init failed @ ");
    vgaPutStr(stage);
    vgaPutStr("\n");
    return -1;
}

int xhciArmInterruptIn(uint8_t slotId) {
    XhciSlot* s = &gSlots[slotId];
    XhTrb t;
    memset(&t, 0, sizeof(t));
    t.dw0 = (uint32_t)(uintptr_t)s->ep1Buf;
    t.dw1 = 0;
    t.dw2 = s->ep1MaxPacket | NORMAL_IOC;
    t.dw3 = (TRB_TYPE_NORMAL << TRB_TYPE_SHIFT);
    ringPut(&s->ep1Ring, &t);
    ringDoorbell(slotId, 3);   /* EP1 IN target = 3 */
    return 0;
}

int xhciRegisterEp1Handler(uint8_t slotId, XhciReportHandler handler) {
    if (slotId >= MAX_SLOTS) return -1;
    gSlots[slotId].reportHandler = handler;
    return 0;
}

/* ============================ 中断处理 ============================ */
static int xhciDispatch(void) {
    int processed = 0;
    while (processOneEvent() >= 0) {
        processed = 1;
        if (gXferDone) {
            uint8_t slot = (uint8_t)gXferSlot;
            uint8_t ep   = (uint8_t)gXferEp;
            uint32_t len = gXferLen;
            gXferDone = false;
            if (slot < MAX_SLOTS && gSlots[slot].inUse && ep == 3
                && gSlots[slot].reportHandler) {
                gSlots[slot].reportHandler(slot, gSlots[slot].ep1Buf, len);
                xhciArmInterruptIn(slot);
            }
        }
    }
    updateErdp();
    return processed;
}

void xhciIRQHandler(void) {
    xhciDispatch();
    /* 清除中断：写 IP=1 清 IMAN，并清 USBSTS.EINT */
    xw32(gRtBase, RT_IMAN(0), 0x1);
    xw32(gOpBase, 0x04, STS_EINT);
    /* 发送 PIC 中断结束（EOI） */
    uint8_t irq = gPci.irqLine;
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
    return;
}

/* ============================ 设备信息（供 HID 层使用） ============================ */
int xhciGetDeviceCount(void) {
    int c = 0;
    for (int i = 0; i < MAX_SLOTS; i++)
        if (gSlots[i].inUse) c++;
    return c;
}

int xhciGetDeviceInfo(uint8_t index, XhciDeviceInfo* info) {
    int seen = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!gSlots[i].inUse) continue;
        if (seen++ == index) {
            info->slotId         = gSlots[i].slotId;
            info->deviceClass    = gSlots[i].deviceClass;
            info->deviceSubclass = gSlots[i].deviceSubclass;
            info->deviceProtocol = gSlots[i].deviceProtocol;
            return 0;
        }
    }
    return -1;
}

/* ============================ 中断使能 ============================ */
static void xhciEnableIrq(void) {
    uint8_t irq = gPci.irqLine;

    /* 使能 interrupter 0 */
    xw32(gRtBase, RT_ERSTSZ(0), 1);           /* ERSTSZ */
    xw32(gRtBase, RT_ERSTBA(0), (uint32_t)(uintptr_t)gErst); /* ERSTBA lo */
    xw32(gRtBase, RT_ERSTBA(0) + 4, (uint32_t)((uintptr_t)gErst >> 32));
    updateErdp();
    xw32(gRtBase, RT_IMAN(0), (1u<<1));       /* IMAN.IE */
    xw32(gRtBase, RT_IMOD(0), 0x3F0);         /* IMOD */

    xw32(gOpBase, 0x04, STS_EINT);                 /* 清 EINT */
    xw32(gOpBase, 0x00, xr32(gOpBase, 0x00) | CMD_INTE);   /* USBCMD.INTE */

    if (irq < 8)
        outb(0x21, inb(0x21) & ~(1u<<irq));
    else
        outb(0xA1, inb(0xA1) & ~(1u<<(irq - 8)));

    /* 在 IDT 注册对应向量（PIC 基址 0x20） */
    extern void irq_xhci_stub(void);
    idtSetGate(0x20 + irq, irq_xhci_stub, 0x08, 0x8E);
}

/* ============================ 初始化 ============================ */
int xhciInit(void) {
    serialPutStr("[USB] scanning PCI for XHCI\n");
    vgaPutStr("[USB] scanning PCI for XHCI\n");
    if (pciFindController(&gPci, PCI_SUBCLASS_USB, PCI_PROGIF_XHCI) != 0) {
        serialPutStr("[USB] no XHCI found\n");
        vgaPutStr("[USB] no XHCI found\n");
        return -1;
    }
    serialPutStr("[USB] XHCI BAR0=");
    vgaPutStr("[USB] XHCI BAR0=");
    serialPutHex32((uint32_t)gPci.bar0Addr);
    vgaPutHex32((uint32_t)gPci.bar0Addr);
    serialPutStr(" IRQ=");
    vgaPutStr(" IRQ=");
    serialPutHex8(gPci.irqLine);
    vgaPutHex8(gPci.irqLine);
    serialPutStr("\n");
    vgaPutStr("\n");

    gBase = (uintptr_t)gPci.bar0Addr;
    /* 映射 MMIO（identity），每页一次 */
    for (uintptr_t a = gBase; a < gBase + gPci.bar0Size; a += PAGE_SIZE) {
        pagingMapPage((uint32_t)a, (uint32_t)a, PAGE_PRESENT | PAGE_WRITABLE);
    }

    uint8_t caplen = *(volatile uint8_t*)gBase;
    uint32_t hcsp1 = *(volatile uint32_t*)(gBase + 0x04);
    uint32_t hcspp1 = *(volatile uint32_t*)(gBase + 0x10);
    gMaxSlots = (uint8_t)(hcsp1 & 0xFF);
    gMaxPorts = (uint8_t)((hcsp1 >> 24) & 0xFF);
    /* HCCPARAMS(0x10) bit2=CSZ，bit0=AC64；QEMU 报 CSZ=0(32字节) */
    gCsz = (uint8_t)((hcspp1 >> 2) & 1);
    gContextSize = gCsz ? 64 : 32;

    gOpBase = gBase + caplen;
    gDbBase = gBase + (*(volatile uint32_t*)(gBase + 0x14) & ~0x3u);   /* DBOFF @0x14 */
    gRtBase = gBase + (*(volatile uint32_t*)(gBase + 0x18) & ~0x1Fu);  /* RTSOFF @0x18 */

    serialPutStr("[XHCI] DBOFF=");
    serialPutHex32(*(volatile uint32_t*)(gBase + 0x14));
    serialPutStr(" RTSOFF=");
    serialPutHex32(*(volatile uint32_t*)(gBase + 0x18));
    serialPutStr(" op=");
    serialPutHex32((uint32_t)gOpBase);
    serialPutStr(" db=");
    serialPutHex32((uint32_t)gDbBase);
    serialPutStr(" rt=");
    serialPutHex32((uint32_t)gRtBase);
    serialPutStr("\n");

    /* 内存分配 */
    gDcbaa = pmmAllocPage();
    memset(gDcbaa, 0, PAGE_SIZE);
    gCmdRing = (volatile XhTrb*)pmmAllocPage();
    gCmdRingPhys = (uintptr_t)gCmdRing;
    gEventSeg = (volatile XhTrb*)pmmAllocPage();
    gErst = (volatile XhTrb*)pmmAllocPage();
    memset((void*)gEventSeg, 0, PAGE_SIZE);
    memset((void*)gErst, 0, PAGE_SIZE);
    for (int i = 0; i < MAX_SLOTS; i++) {
        gCtxPool[i] = pmmAllocPage();
        gInCtxPool[i] = pmmAllocPage();
        gEp0BufPool[i] = (uint8_t*)pmmAllocPage();
        gEp1BufPool[i] = (uint8_t*)pmmAllocPage();
        gRingPool[i][0] = pmmAllocPage();
        gRingPool[i][1] = pmmAllocPage();
        memset(gCtxPool[i], 0, PAGE_SIZE);
        memset(gInCtxPool[i], 0, PAGE_SIZE);
    }

    /* 事件段初始化 */
    memset((void*)gEventSeg, 0, PAGE_SIZE);
    if (gMaxSlots > MAX_SLOTS) gMaxSlots = MAX_SLOTS;

    /* 复位控制器 */
    xw32(gOpBase, 0x00, CMD_HCRST);
    for (int i = 0; i < 100000; i++) {
        if (!(xr32(gOpBase, 0x04) & STS_HCH)) break;
        for (volatile int j = 0; j < 1000; j++);
    }
    xw32(gOpBase, 0x04, STS_HCE);   /* 清 Host Controller Error */
    xw32(gOpBase, 0x04, STS_EINT | STS_PCD);

    /* 命令环：CRCR = ring | RCS(bit0=1)，初始环周期状态必须为 1 */
    xw32(gOpBase, 0x18, (uint32_t)(gCmdRingPhys & ~0xF) | 0x1);
    xw32(gOpBase, 0x1C, 0);
    /* DCBAAP */
    xw32(gOpBase, 0x30, (uint32_t)(uintptr_t)gDcbaa);
    xw32(gOpBase, 0x34, 0);
    /* CONFIG = MaxSlotsEn */
    xw32(gOpBase, 0x38, gMaxSlots);

    /* 事件环：填写 ERST 段描述符（gErst[0]），再让 ERSTBA 指向 gErst */
    uintptr_t segPhys = (uintptr_t)gEventSeg;
    volatile uint32_t* erst = (volatile uint32_t*)gErst;
    erst[0] = (uint32_t)segPhys;              /* 段基底低 32 位 */
    erst[1] = (uint32_t)(segPhys >> 32);      /* 段基底高 32 位 */
    erst[2] = RING_SIZE;                       /* 段大小（TRB 数） */
    erst[3] = 0;
    xw32(gRtBase, RT_ERSTSZ(0), 1);           /* ERSTSZ=1 */
    xw32(gRtBase, RT_ERSTBA(0), (uint32_t)(uintptr_t)gErst); /* ERSTBA lo */
    xw32(gRtBase, RT_ERSTBA(0) + 4, (uint32_t)((uintptr_t)gErst >> 32));
    xw32(gRtBase, RT_ERDP(0), (uint32_t)segPhys); /* ERDP 指向事件段起始 */
    xw32(gRtBase, RT_ERDP(0) + 4, 0);
    serialPutStr("[XHCI] segPhys=");
    serialPutHex32((uint32_t)segPhys);
    serialPutStr(" bar0Size=");
    serialPutHex32(gPci.bar0Size);
    serialPutStr(" IMAN=");
    serialPutHex32((uint32_t)xr32(gRtBase, RT_IMAN(0)));
    serialPutStr(" ERSTSZ=");
    serialPutHex32((uint32_t)xr32(gRtBase, RT_ERSTSZ(0)));
    serialPutStr(" ERSTBA=");
    serialPutHex32((uint32_t)xr32(gRtBase, RT_ERSTBA(0)));
    serialPutStr(" ERDP=");
    serialPutHex32((uint32_t)xr32(gRtBase, RT_ERDP(0)));
    serialPutStr("\n");

    /* 运行 */
    xw32(gOpBase, 0x00, xr32(gOpBase, 0x00) | CMD_RS);
    for (int i = 0; i < 100000; i++) {
        if (!(xr32(gOpBase, 0x04) & STS_HCH)) break;
        for (volatile int j = 0; j < 1000; j++);
    }
    serialPutStr("[XHCI] USBCMD=");
    serialPutHex32((uint32_t)xr32(gOpBase, 0x00));
    serialPutStr(" USBSTS=");
    serialPutHex32((uint32_t)xr32(gOpBase, 0x04));
    serialPutStr("\n");

    /* 扫描各根端口，枚举已连接的设备 */
    int found = 0;
    for (uint8_t p = 1; p <= gMaxPorts; p++) {
        uint32_t portsc = xr32(gOpBase, 0x400 + (uint32_t)p * 0x10);
        if (portsc & 1) {   /* CCS：有设备连接 */
            serialPutStr("[USB] port connected\n");
            vgaPutStr("[USB] port connected\n");
            uint8_t speed = (uint8_t)(((portsc >> 10) & 0xF) + 1);  /* PORTSC→slot ctx Speed */
            if (enumeratePort(p, speed) == 0) {
                found++;
            }
        }
    }

    serialPutStr("[USB] init done, devices=");
    vgaPutStr("[USB] init done, devices=");
    serialPutHex8((uint8_t)found);
    vgaPutHex8((uint8_t)found);
    serialPutStr("\n");
    vgaPutStr("\n");

    /* 使能中断，开始中断驱动的 HID 输入 */
    xhciEnableIrq();
    return found ? 0 : -1;
}