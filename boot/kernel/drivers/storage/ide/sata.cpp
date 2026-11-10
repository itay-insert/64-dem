#include <stdbool.h>
#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/memory/memory.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"

#define CAP_NP 0x1f
#define CAP_NCS(x) (((x) & 0x1fff) >> 8)
#define CAP_S64A 0x80000000

#define CAP2_BOH (1u << 0)

#define BOHC_BOS (1u << 0)
#define BOHC_OOS (1u << 1)
#define BOHC_BB (1u << 4)

#define ATA 0
#define ATAPI 1

typedef struct {
    u32 CAP;
    u32 GHC;
    u32 IS;
    u32 PI;
    u32 VS;
    u32 Padding0;
    u32 Padding1;
    u32 Padding2;
    u32 Padding3;
    u32 CAP2;
    u32 BOHC;
} __attribute__((packed)) ahci_cap_regs;


typedef struct {
    u32 CLB;
    u32 CLBU;
    u32 FB;
    u32 FBU;
    u32 IS;
    u32 IE;
    u32 CMD;
    u32 Reserved;
    u32 TFD;
    u32 SIG;
    u32 SSTS;
    u32 SCTL;
    u32 SERR;
    u32 SACT;
    u32 CI;
} __attribute__((packed)) ahci_port;

typedef struct {
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u64 io_phys;
    u64 io_virt;
    pci_address_t address;
    bool discovered;
} ahci_descriptor;

typedef struct {
    u8 port_number;
    u8 device_type;
    bool connected;
    bool device_present;
    bool initialized;
    u32 sector_size;
    u64 capacity;
    char serial[21];
    char firmware[9];
    char model[41]; 
    u8 packet_size;
    dma_ret Command_list;
    dma_ret Command_Table;
    dma_ret identify;
    dma_ret FIS;
    dma_ret data;
} port_descriptor;


typedef struct {
    u16 flags;       // CFL, A, W, P, R, B, C, PMP
    u16 prdtl;       // Number of PRDT entries
    u32 prdbc;       // Bytes transferred
    u32 ctba;        // Command table address (low)
    u32 ctbau;       // Command table address (high)
    u32 reserved[4];
} __attribute__((packed)) AHCI_command_header;

static_assert(sizeof(AHCI_command_header) == 32);

typedef struct {
    u32 DBA;         // Data Base Address (low)
    u32 DBAU;        // Data Base Address (high)
    u32 reserved;
    u32 DBC;         // Byte count minus one; bit 31 = IOC
} __attribute__((packed)) AHCI_PRDT;

static_assert(sizeof(AHCI_PRDT) == 16);

typedef struct {
    u8 CFIS[64];       // Command FIS
    u8 ACMD[16];       // ATAPI command
    u8 reserved[48];   // Reserved
    AHCI_PRDT PRDT[1]; // PRDT entries
} __attribute__((packed, aligned(128))) AHCI_command_table;

typedef struct {
    AHCI_command_header headers[32];
} __attribute__((packed, aligned(1024))) AHCI_command_list;

static_assert(sizeof(AHCI_command_list) == 1024);

typedef struct {
    u8 DMA_setup[0x1C];
    u8 reserved1[0x04];

    u8 PIO_setup[0x14];
    u8 reserved2[0x0C];

    u8 D2H_register[0x14];
    u8 reserved3[0x04];

    u8 set_device_bits[0x08];
    u8 unknown[0x40];

    u8 reserved4[0x60];
} __attribute__((packed, aligned(256))) AHCI_received_FIS;

static_assert(sizeof(AHCI_received_FIS) == 256);

port_descriptor *ports = nullptr;
ahci_descriptor ahci = {0};

u8 port_count = 0;
u8 cmd_slots = 0;

bool addr_64 = false;

void ahci_find_callback(const PCI_ret *device, void *context) {
    ahci_descriptor *desc = (ahci_descriptor *)context;

    if (desc->discovered || device->PCI_status != PCI_STATUS_SUCCESS)
        return;

    if (device->common.class_code != desc->class_code || 
        device->common.subclass != desc->subclass ||
        device->common.prog_if != desc->prog_if)
        return;

    printf("SATA contoller found at pci address %u:%u:%u.%u\n", device->Segment, 
    device->Bus, device->Device, device->Function);
    desc->address = (pci_address_t) {
        .segment = device->Segment,
        .bus = device->Bus,
        .device = device->Device,
        .function = device->Function
    };

    desc->discovered = true;
}


static bool discover_ahci(void) {
    ahci_descriptor *desc = &ahci;
    *desc = {};

    desc->class_code = 0x01;
    desc->subclass = 0x06;
    desc->prog_if = 0x01;

    // discover pci device
    pci_enumerate(ahci_find_callback, desc);

    if (!desc->discovered)
        return false;

    pci_bar_t bar = {0};
    if (pci_read_bar(desc->address, 5, 0, &bar) != PCI_STATUS_SUCCESS)
        return false;

    // read the address from BAR5
    u64 addr = bar.address;
    desc->io_phys = addr;
    desc->io_virt = desc->io_phys + BASE;

    u64 page_phys = addr & ~4095ULL;
    u64 offset = addr - page_phys;

    u64 pages = (offset + bar.size + 4095) >> 12;

    // map the address to the MMIO region
    create_mapping(page_phys+BASE, page_phys, pages, 0x13, KernelPML4);
    flush_pages(page_phys+BASE, pages);

}


static inline bool ahci_take_ownership(TimerSource &timer, volatile ahci_cap_regs *regs) {
    if (!(regs->CAP2 & CAP2_BOH))
        return true;

    regs->BOHC = (regs->BOHC & ~BOHC_OOS) | BOHC_OOS; // set OOS bit
    
    bool bios_busy = false;
    for (u32 elapsed = 0; elapsed < 25; elapsed++) { // 25 ms timeout for BOHC_BB
        if (!(regs->BOHC & BOHC_BOS))
            return true;

        if (regs->BOHC & BOHC_BB) {
            bios_busy = true;
            break;
        }

        wait_ms(timer, 1);
    }

    if (!bios_busy) {
        if (!(regs->BOHC & BOHC_BOS))
            return true;

        if (!(regs->BOHC & BOHC_BB))
            return true;

        bios_busy = true;
    }


    for (u32 elapsed = 0; elapsed < 5000; elapsed++) {
        if (!(regs->BOHC & BOHC_BOS))
            return true; // handoff completed successfully
        
        wait_ms(timer, 1);
    }

   return !(regs->BOHC & BOHC_BOS);
}


static inline u64 port_to_address(u32 port) {
    u64 port_base = ahci.io_virt + 0x100 + (port * 0x80);
    return port_base;
}

#define SCTL_DET_MASK 0xF
#define SCTL_DET_INIT 0x1
#define SCTL_DET_OFF 4

#define CMD_ST (1u << 0)
#define CMD_CR (1u << 15)
#define CMD_FRE (1u << 4)
#define CMD_FR (1u << 14)


static inline bool stop_port(volatile ahci_port *port, TimerSource &timer) {
    u32 cmd = port->CMD; // save the CMD register

    // stop command processing
    port->CMD = cmd & ~CMD_ST;

    // wait for engine to stop
    bool stopped = false;
    for (u32 elapsed = 0; elapsed < 500; elapsed++) {  // 500 ms timeout
        if (!(port->CMD & CMD_CR)) {
            stopped = true;
            break;
        }

        wait_ms(timer, 1); // wait 1 ms
    }


    if (!stopped)
        return false;
    else {
        port->CMD |= CMD_FRE;

        // clear previous errors
        port->SERR = 0xFFFFFFFF;
        port->IS = 0xFFFFFFFF;

        return true;
    }
}


static inline bool issue_reset(volatile ahci_port *port, TimerSource &timer) {
    if (!stop_port(port, timer)) {
        printf("SATA: failed to stop port\n");
        return false;
    }

    // assert COMRESET
    u32 sctl = port->SCTL;

    port->SCTL = (sctl & ~SCTL_DET_MASK) | SCTL_DET_INIT;

    wait_ms(timer, 1); // wait 1 milesecond

    port->SCTL = sctl & ~SCTL_DET_MASK;

    // wait for sata connection
    bool connected = false;
    for (u32 elapsed = 0; elapsed < 5000; elapsed++) {
        u32 det = port->SSTS & 0xf;
        if (det == 3) {
            connected = true;
            break;
        }

        wait_ms(timer, 1);
    }

    if (!connected) {
        printf("SATA: link connection while asserting COMRESET failed\n");
        return false;
    }

    // wait for signature
    bool identified = false;

    for (u32 elapsed = 0; elapsed < 5000; elapsed++) {
        if (port->SIG != 0xFFFFFFFF) {
            identified = true;
            break;
        }

        wait_ms(timer, 1);
    }

    if (!identified) {
        printf("SATA: error: controller didn't update PxSIG\n");
        return false;
    }

    return true;
}

static bool SATA_start_engine(volatile ahci_port *port, TimerSource &timer) {
    // wait until command list engine is stopped
    for (u32 elapsed = 0; elapsed < 1000; elapsed++) { // 1000 ms timeout
        if (!(port->CMD & CMD_CR))
            break;

        wait_ms(timer, 1);
    }

    if (port->CMD & CMD_CR)
        return false;

    // enable recieved FIS
    port->CMD |= CMD_FRE;

    for (u32 elapsed = 0; elapsed < 1000; elapsed++) { // 1000 ms timeout
        if (port->CMD & CMD_FR)
            break;

        wait_ms(timer, 1);
    }

    if (!(port->CMD & CMD_FR))
        return false;

    if (port->TFD & ((1u << 7) | (1u << 3)))
        return false; // return if device is busy or BRQ

    port->CMD |= CMD_ST;

    return true; // start_engine success
}

#define IS_TFES (1u << 30)

static bool ATA_issue_command(volatile ahci_port *port, u32 slot, TimerSource &timer) {
    u32 mask = 1u << slot;

    if (!(port->CMD & CMD_ST))
        return false; // engine must be running

    if (port->CI & mask)
        return false; // slot must not already be active

    if (port->TFD & ((1u << 7) | (1u << 3)))
        return false; // return if device is busy or BRQ

    // clear previous task-file errors
    port->IS = IS_TFES;

    // submit command
    port->CI = mask;

    for (u32 elapsed = 0; elapsed < 5000; elapsed++) {
        if (!(port->CI & mask))
            break;

        if (port->IS & IS_TFES)
            return false;

        wait_ms(timer, 1);
    }

    if (port->CI & mask) {
        printf("SATA: command timeout\n");
        return false;
    }

    if (port->IS & IS_TFES)
        return false;

    if (port->TFD & 1u)
        return false;

    return true;
}


static void ATA_identify_string(const u16 *identify, u32 first_word, u32 word_count, char *output) {
    for (u32 i = 0; i < word_count; i++) {
        u16 word = identify[first_word + i];

        output[i * 2] = (char)(word >> 8);
        output[i * 2 + 1] = (char)(word & 0xFF);
    }

    u32 length = word_count * 2;
    output[length] = '\0';

    while (length > 0 && output[length - 1] == ' ')
        output[--length] = '\0';

}


static u64 ATA_identify_capacity(const u16 *id) {
    if (!(id[0] & (1 << 9)))
        return 0;

    return (u32)id[60] | ((u32)id[61] << 16);
}


static u32 ATA_identify_sector_size(const u16 *id) {
    u16 w106 = id[106];

    // Check validity of word 106.
    if ((w106 & 0xC000) != 0x4000)
        return 512;

    // Bit 12: logical sector is larger than 256 words.
    if (!(w106 & (1u << 12)))
        return 512;

    u32 words = (u32)id[117] | ((u32)id[118] << 16);

    if (!words)
        return 512;

    return words * 2;
}


static bool ATA_identify(u32 port, volatile ahci_port *port_reg, TimerSource &timer) {
    dma_ret CMD_LIST = ports[port].Command_list;
    dma_ret CMD_TBL = ports[port].Command_Table;
    dma_ret FIS = ports[port].FIS;
    dma_ret IDENTIFY = ports[port].identify;

    AHCI_command_header *header = (AHCI_command_header *)CMD_LIST.virtual_address;

    header->flags = 5;
    header->prdtl = 1;
    header->prdbc = 0;

    u64 table_phys = CMD_TBL.physical_address;
    header->ctba = (u32)table_phys;  // put CMD_TBL physical in the low and high registers of ctba
    header->ctbau = (u32)(table_phys >> 32);

    AHCI_command_table *cmd_table = (AHCI_command_table *)CMD_TBL.virtual_address;
    // zero-initialize the command table
    memset(cmd_table, 0, 144);

    u8 *fis = cmd_table->CFIS;

    fis[0] = 0x27;
    fis[1] = 1 << 7;
    fis[2] = 0xEC; // ATA_IDENTIFY

    u64 identify_phys = IDENTIFY.physical_address;

    cmd_table->PRDT[0].DBA = (u32)identify_phys;
    cmd_table->PRDT[0].DBAU = (u32)(identify_phys >> 32);
    cmd_table->PRDT[0].reserved = 0;
    cmd_table->PRDT[0].DBC = 511;

    if ((port_reg->SSTS & 0xF) != 3)
        return false;

    for (u32 elapsed = 0; elapsed < 5000; elapsed++) {
        if (!(port_reg->TFD & ((1u << 7) | (1u << 3))))
            break;

        wait_ms(timer, 1);
    }

    if (port_reg->TFD & ((1u << 7) | (1u << 3))) {
        printf("SATA: wait for idle timeout\n");
        return false;
    }

    port_reg->IS = 0xFFFFFFFF;
    port_reg->SERR = 0xFFFFFFFF;

    if (!(port_reg->CMD & CMD_FRE) && (port_reg->CMD & CMD_ST))
        return false;

    if (!(port_reg->CMD & CMD_ST) || !(port_reg->CMD & CMD_FRE)) {
        if (!SATA_start_engine(port_reg, timer)) {
            printf("SATA: failed to start engine\n");
            return false;
        }
    }




    if (!ATA_issue_command(port_reg, 0, timer)) 
        return false;

    u16 *id = (u16 *)IDENTIFY.virtual_address;

    ATA_identify_string(id, 10, 10, ports[port].serial);
    ATA_identify_string(id, 23, 4, ports[port].firmware);
    ATA_identify_string(id, 27, 20, ports[port].model);

    ports[port].capacity = ATA_identify_capacity(id);
    ports[port].sector_size = ATA_identify_sector_size(id);
    ports[port].device_type = ATA;

    ports[port].initialized = true;
    return true;
}

static bool ATAPI_get_packet_size(
    const u16 *identify,
    u8 &packet_size
) {
    u16 config = identify[0];

    // Verify this is an ATAPI device (bits 15:14 = 10b).
    if ((config & 0xC000) != 0x8000)
        return false;

    switch (config & 0x3) {
        case 0:
            packet_size = 12;
            return true;

        case 1:
            packet_size = 16;
            return true;

        default:
            return false; // Reserved packet size
    }
}

static bool ATAPI_identify(u32 port, volatile ahci_port *port_reg, TimerSource &timer) {
    dma_ret CMD_LIST = ports[port].Command_list;
    dma_ret CMD_TBL = ports[port].Command_Table;
    dma_ret FIS = ports[port].FIS;
    dma_ret IDENTIFY = ports[port].identify;

    AHCI_command_header *header = (AHCI_command_header *)CMD_LIST.virtual_address;

    header->flags = 5;
    header->prdtl = 1;
    header->prdbc = 0;

    u64 table_phys = CMD_TBL.physical_address;
    header->ctba = (u32)table_phys;  // put CMD_TBL physical in the low and high registers of ctba
    header->ctbau = (u32)(table_phys >> 32);

    AHCI_command_table *cmd_table = (AHCI_command_table *)CMD_TBL.virtual_address;
    // zero-initialize the command table
    memset(cmd_table, 0, 144);

    u8 *fis = cmd_table->CFIS;

    fis[0] = 0x27;
    fis[1] = 1 << 7;
    fis[2] = 0xA1; // ATAPI_IDENTIFY

    u64 identify_phys = IDENTIFY.physical_address;

    cmd_table->PRDT[0].DBA = (u32)identify_phys;
    cmd_table->PRDT[0].DBAU = (u32)(identify_phys >> 32);
    cmd_table->PRDT[0].reserved = 0;
    cmd_table->PRDT[0].DBC = 511;

    if ((port_reg->SSTS & 0xF) != 3)
        return false;

    for (u32 elapsed = 0; elapsed < 5000; elapsed++) {
        if (!(port_reg->TFD & ((1u << 7) | (1u << 3))))
            break;

        wait_ms(timer, 1);
    }

    if (port_reg->TFD & ((1u << 7) | (1u << 3))) {
        printf("SATA: wait for idle timeout\n");
        return false;
    }

    port_reg->IS = 0xFFFFFFFF;
    port_reg->SERR = 0xFFFFFFFF;

    if (!(port_reg->CMD & CMD_FRE) && (port_reg->CMD & CMD_ST))
        return false;

    if (!(port_reg->CMD & CMD_ST) || !(port_reg->CMD & CMD_FRE)) {
        if (!SATA_start_engine(port_reg, timer)) {
            printf("SATA: failed to start engine\n");
            return false;
        }
    }




    if (!ATA_issue_command(port_reg, 0, timer)) 
        return false;

    u16 *id = (u16 *)IDENTIFY.virtual_address;

    ATA_identify_string(id, 10, 10, ports[port].serial);
    ATA_identify_string(id, 23, 4, ports[port].firmware);
    ATA_identify_string(id, 27, 20, ports[port].model);

    ports[port].capacity = 0;
    ports[port].sector_size = 0;
    ports[port].device_type = ATAPI;
    if (!ATAPI_get_packet_size(id, ports[port].packet_size))
        return false;

    return true;
}


static bool ATAPI_packet(u32 port, volatile ahci_port *port_reg, const u8 *cdb, u32 cdb_length, dma_ret *buffer, u32 data_length, bool host_to_device, TimerSource &timer) {
     if (!cdb || cdb_length == 0)
        return false;

    u32 packet_size = ports[port].packet_size;

    if ((packet_size != 12 && packet_size != 16) ||
        cdb_length > packet_size)
        return false;

    if (data_length && !buffer)
        return false;

    // One PRDT entry: maximum 4 MiB.
    if (data_length > 0x400000)
        return false;

    // Retrieve ATAPI DMA capabilities.
    const u16 *id = (const u16 *)
        ports[port].identify.virtual_address;

    bool dmadir_required = id[62] & 0x8000;

    bool dma_supported = dmadir_required
        ? (id[62] & (1u << 10))
        : (id[49] & (1u << 8));

    if (data_length && !dma_supported) {
        printf("ATAPI: DMA not supported\n");
        return false;
    }

    dma_ret CMD_LIST = ports[port].Command_list;
    dma_ret CMD_TBL  = ports[port].Command_Table;

    auto *header =
        (AHCI_command_header *)CMD_LIST.virtual_address;

    auto *cmd_table =
        (AHCI_command_table *)CMD_TBL.virtual_address;

    // Prepare command header.
    memset(header, 0, sizeof(*header));

    header->flags = 5 | AHCI_CMD_A;

    if (data_length && host_to_device)
        header->flags |= AHCI_CMD_W;

    header->prdtl = data_length ? 1 : 0;
    header->prdbc = 0;

    u64 table_phys = CMD_TBL.physical_address;

    header->ctba  = (u32)table_phys;
    header->ctbau = (u32)(table_phys >> 32);

    // Prepare command table.
    memset(cmd_table, 0, 144);

    // Register Host-to-Device FIS.
    u8 *fis = cmd_table->CFIS;

    fis[0] = 0x27;
    fis[1] = 1u << 7;
    fis[2] = ATA_PACKET;

    // Configure DMA data transfer.
    if (data_length) {
        fis[3] = ATAPI_DMA;

        // DMADIR is only required by certain devices.
        // 1 means device -> host.
        if (dmadir_required && !host_to_device)
            fis[3] |= ATAPI_DMADIR;
    }

    // Copy SCSI CDB into ACMD.
    // The zero-initialized remainder pads it to packet_size.
    memcpy(cmd_table->ACMD, cdb, cdb_length);

    // Configure data buffer if this command has a data phase.
    if (data_length) {
        u64 buffer_phys = buffer->physical_address;

        if (buffer_phys & 1)
            return false;

        cmd_table->PRDT[0].DBA = (u32)buffer_phys;
        cmd_table->PRDT[0].DBAU = (u32)(buffer_phys >> 32);
        cmd_table->PRDT[0].reserved = 0;
        cmd_table->PRDT[0].DBC = data_length - 1;
    }

    // Clear previous status.
    port_reg->IS = 0xFFFFFFFF;
    port_reg->SERR = 0xFFFFFFFF;

    // Ensure DMA structures are visible to the HBA.
    // Perform the appropriate DMA synchronization/barrier.
    asm volatile("mfence" ::: "memory");

    // Issue PACKET command.
    return ATA_issue_command(port_reg, 0, timer);
}


static u32 read_be32(const u8 *p) {
    return ((u32)p[0] << 24) |
           ((u32)p[1] << 16) |
           ((u32)p[2] << 8)  |
           ((u32)p[3]);
}


static bool ATAPI_init(u32 port, volatile ahci_port *port_reg, TimerSource &timer) {
    if (!ATAPI_identify(port, port_reg, timer))
        return false;

    u8 cdb[6] = {
        0x12, // INQUIRY
        0x00,
        0x00,
        0x00,
        36, 
        0x00
    };

    dma_ret dma = ports[port].data;

    if (!ATAPI_packet(port, port_reg, cdb, sizeof(cdb), &dma, 36, false, timer)) {
        printf("SATA: INQUIRY Failed\n");
        return false;
    }

    u8 cdb1[6] = { 0x00 };

    bool try_again = true;

    again:


    if (!ATAPI_packet(port, port_reg, cdb1, sizeof(cdb1), nullptr, 0, false, timer)) {
        printf("SATA: TEST UNIT Failed, Requesting sense\n");
        u8 cdb2[6] = {0x03, 0x00, 0x00, 18, 0x00};

        if (!ATAPI_packet(port, port_reg, cdb2, sizeof(cdb2), &dma, 18, false, timer)) {
            printf("SATA: REQUEST SENSE failed\n");
            return false;
        }

        u8 *sense = (u8 *)dma.virtual_address;

        u8 sense_key = sense[2] & 0x0F;
        u8 asc = sense[12];
        u8 ascq = sense[13];
        
        if (sense_key == 0x02 && asc == 0x04 && ascq == 0x01) {
            if (try_again) {
                try_again = false;
                printf("SATA: trying test unit again\n");
                goto again;
            }
        } else {
            printf("SATA: test unit failed\n");
            return false;
        }
    }

    u8 cdb3[10] = {};
    cdb[0] = 0x25; // read capacity


    if (!ATAPI_packet(port, port_reg, cdb3, sizeof(cdb3), &dma, 8, false, timer)) {
        printf("SATA: READ CAPACITY failed\n");
        return false;
    }

    u8 *data = (u8 *)dma.virtual_address;

    u32 last_lba = read_be32(data);
    u32 block_size = read_be32(data + 4);

    ports[port].sector_size = block_size;
    ports[port].capacity = last_lba + 1;
    ports[port].initialized = true;

    return true;
}


static bool ahci_enumrate_port(u32 port, TimerSource &timer) {
    volatile ahci_port *port_reg = (volatile ahci_port *)port_to_address(port);

    dma_ret CMD_LIST = allocate_dma(1024);
    if (CMD_LIST.status != 0) {
        printf("SATA: failed to allocate command list for port %u\n", port);
        return false;
    } 

    ports[port].Command_list = CMD_LIST;

    dma_ret CMD_TBL = allocate_dma(144);
    if (CMD_TBL.status != 0) {
        printf("SATA: failed to allocate CMD_TBL for port %u\n", port);
        return false;
    }

    ports[port].Command_Table = CMD_TBL;

    dma_ret IDENTIFY = allocate_dma(512);
    if (IDENTIFY.status != 0) {
        printf("SATA: failed to allocate IDENTIFY for port %u\n", port);
        return false;
    }

    ports[port].identify = IDENTIFY;


    dma_ret FIS = allocate_dma(256);
    if (FIS.status != 0) {
        printf("SATA: FIS dma allocation for port %u failed\n", port);
        return false;
    }


    ports[port].FIS = FIS;


    memset((void *)FIS.virtual_address, 0, 256);


    port_reg->FB = (u32)FIS.physical_address;
    port_reg->FBU = (u32)(FIS.physical_address >> 32);

    u32 det = port_reg->SSTS & 0xf;

    switch (det) {
        case 0:
            ports[port] = {0};
            ports[port].device_present = false;
            break;

        case 1:
            u32 sctl = port_reg->SCTL;
            // assert COMRESET
            port_reg->SCTL = (sctl & ~0xFu) | 1u;

            wait_ms(timer, 1);

            port_reg->SCTL = sctl & ~0xFu;

            for (u32 elapsed = 0; elapsed < 1000; elapsed++) {
                if ((port_reg->SSTS & 0xF) == 3) {
                    ports[port].connected = true;
                    break;
                }

                wait_ms(timer, 1);
            }

            if (!ports[port].connected) {
                printf("SATA: link initialization for port 0x%b failed or timed out\n", port);
                return false;
            }

            port->SERR = 0xFFFFFFFF;

            goto next;

        case 2:
            printf("SATA: incorrect det value for port 0x%b\n", port);
            return false;

        case 3:
            goto next;

        case 4:
            if ((sctl & SCTL_DET_MASK) == SCTL_DET_OFF) {
                // software disabled the SATA PHY.
                u32 cmd = port_reg->CMD; // read the port's command register
                
                if (cmd & CMD_ST) {
                    port->CMD = cmd & ~CMD_ST;

                    bool stopped = false;
                    for (u32 i = 0; i < 1000; i++) {// 1 second timeout
                        if (!(port->CMD & CMD_CR)) {
                            stopped = true;
                            break;
                        }

                        wait_ms(timer, 1);
                    }

                    if (!stopped)
                        return false;
                }

                u32 sctl = port_reg->SCTL;
                port_reg->SCTL = sctl & ~SCTL_DET_MASK;

                for (u32 i = 0; i < 2000; i++) {

                    u32 det = port_reg->SSTS & 0xF;

                    if (det == 3)
                        goto next;

                    wait_ms(timer, 1);
                }

                return false;
            } else {
                printf("SATA: offline condition disabled the SATA PHY\n");
                return false;
            }
            
        default:
            printf("SATA: incorrect det value for port 0x%b\n", port);
            return false;
    }

    next:

    bool try_again = true;

    SigAgain:

    u32 sig = port->SIG;

    if (sig == 0xFFFFFFFF) {
        // try reset
        if (try_again && issue_reset(port_reg, timer)) {
            try_again = false;
            goto SigAgain;
        }

        return false;
    } else {
        switch (sig) {
            case 0x00000101:
                return ATA_identify(port, port_reg, timer);
            
            
            case 0xEB140101:
                // ATAPI device
                dma_ret ATAPI_dma = allocate_dma(4096);
                if (ATAPI_dma.status != 0)
                    return false;
                ports[port].data = ATAPI_dma;
                return ATAPI_init(port, port_reg, timer);

            case 0xC33C0101:
                return false;

                

            case 0x96690101:
                return false;

            default:
                return false;

                break;
        }
    }
    
}

bool controller_init(TimerSource &timer) {
    if (!discover_ahci())
        return false;

    
    volatile ahci_cap_regs *cap_regs = (volatile ahci_cap_regs *)ahci.io_virt;

    u32 cap = cap_regs->CAP;
    port_count = (u32)(cap & CAP_NP); // get number of ports - 1
    port_count++;

    ports = new port_descriptor[port_count];

    cmd_slots = (u8)CAP_NCS(cap); // get number of command slots - 1
    addr_64 = (cap & CAP_S64A);  // get whether 64-bit dma addresses are supported

    if (!ahci_take_ownership(timer, cap_regs)) {
        printf("SATA: failed to obtain ownership\n");
        return false;
    }

    cap_regs->GHC |= (1u << 31); // enable ahci mode



}