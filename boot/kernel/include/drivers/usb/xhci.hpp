#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/xhci_events.h"
#include "drivers/usb/usb.hpp"


#define TIMEOUT 1000000

#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_STS_HCH (1u << 0)
#define XHCI_STS_CNR (1u << 11)


#define XHCI_EXT_CAP_LEGACY 1
#define XHCI_LEGACY_BIOS_OWNED (1u << 16)
#define XHCI_LEGACY_OS_OWNED (1u << 24)

#define XHCI_LEGACY_SMI_ENABLES ((1u << 0) | (1u << 4) | (1u << 13) | (1u << 14) | (1u << 15))

#define CCS (1 << 0) // current connect status
#define PED (1 << 1) // port enabled
#define OCA (1 << 3) // over-current active
#define PR (1 << 4) // port reset
#define PLS_MASK (0xF << 5) // port link state
#define U0 (0 << 5) // U0 link state
#define PRC (1 << 21) // port reset change

#define RESET_TIMEOUT 1000

#define RO_MASK ((1 << 0) | (1 << 3) | (0xF << 10) | (1 << 30))
#define RWS_MASK ((0xF << 5) | (1 << 9) | (0x3 << 14) | (0x7 << 25))

#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORTSC_STRIDE 0x10


typedef struct {
    uint8_t  caplength;
    uint8_t  reserved;
    uint16_t hciversion;

    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hcsparams3;

    uint32_t hccparams1;

    uint32_t dboff;
    uint32_t rtsoff;

    uint32_t hccparams2;
} xhci_cap_regs;


typedef struct {
    uint32_t usbcmd;        
    uint32_t usbsts;        
    uint32_t pagesize;      
    uint32_t rsvd0[2];      
    uint32_t dnctrl;        
    uint64_t crcr;          
    uint32_t rsvd1[4];      
    uint64_t dcbaap;        
    uint32_t config;        
    uint32_t rsvd2[241];
} xhci_op_regs;



struct XhciTrb {
    u32 d0, d1, d2, d3;
};

static_assert(sizeof(XhciTrb) == 16);

struct XhciErstEntry {
    u64 segment_base;
    u32 segment_size_trbs;
    u32 reserved;
};
static_assert(sizeof(XhciErstEntry) == 16);

struct XhciRings {
    dma_ret command_dma;
    dma_ret event_dma;
    dma_ret erst_dma;

    XhciTrb *command;
    XhciTrb *event;

    u64 doorbell_base;
    u64 interrupter0_base;
    bool ac64;

    u32 command_index = 0;
    u32 command_cycle = 1;
    u32 event_index = 0;
    u32 event_cycle = 1;
};

class xhci_controller : public usb_controller {
    private:
        u64 *scratch_pad = 0;
        u64 *dcbaa = 0;
        u64 scratchpad_physical = 0;

        u64 xhci_base = 0;

        XhciRings rings; // Keep this state after xhci_init() returns.


        u64 pending_command_physical = 0;
        XhciTrb pending_command_completion = {};
        u32 pending_command_done = 0;
        u32 interrupt_driven = 0;
        u32 processing_events = 0;
        u32 pending_port_changes[8] = {};

        bool setup_rings(volatile xhci_cap_regs *cap,
                          volatile xhci_op_regs *op, bool ac64);
        u64 submit_command(XhciTrb command);
        bool poll_event(XhciTrb *out);
        u32 xhci_process_events(void);
        int xhci_take_port_change(u8 port_number);
        void xhci_set_interrupt_driven(int enabled);
        bool wait_command(TimerSource& timer, u64 command_physical,
                           XhciTrb *completion, u32 timeout_ms);
        int xhci_reset();
        volatile u32 *xhci_find_extended_capability(volatile xhci_cap_regs *cap, 
u8 wanted_id);
        bool xhci_take_ownership(volatile xhci_cap_regs *cap, TimerSource& timer);
        
        int allocate_scracthpad(u32 scratchpad_count, bool addr_64);
                        
    public:
        int controller_init(TimerSource &timer) override;
        int enumrate_ports();
        int reset_port(u8 port, TimerSource &timer);
        xhci_controller();

};