#include <stdbool.h>
#include <stddef.h>
#include "uint_definitions.h"
#include "x86-64/paging.h"
#include "drivers/display/vga.h"
#include "drivers/pci/pci.h"
#include "drivers/timers/timer.hpp"
#include "x86-64/memory/kmalloc.h"
#include "drivers/storage/ide/sata.hpp"

#define CAP_NP 0x1f
#define CAP_NCS(x) (((x) & 0x1fff) >> 8)
#define CAP_S64A 0x80000000

#define CAP2_BOH (1u << 0)

#define BOHC_BOS (1u << 0)
#define BOHC_OOS (1u << 1)
#define BOHC_BB (1u << 4)

#define ATA 0
#define ATAPI 1

// AHCI command-header flags.
#define AHCI_CMD_A (1u << 5) // ATAPI command
#define AHCI_CMD_W (1u << 6) // Host-to-device data transfer

// ATA PACKET opcode and Features register bits.
#define ATA_PACKET 0xA0
#define ATAPI_DMA (1u << 0)
#define ATAPI_DMADIR (1u << 2) // Device-to-host DMA direction

extern u64 timeradd;

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

u8 current_port = 0;

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
    // Probe the BAR size: without probing, pci_read_bar leaves size at zero
    // and a page-aligned ABAR would result in a zero-page mapping.
    if (pci_read_bar(desc->address, 5, 1, &bar) != PCI_STATUS_SUCCESS)
        return false;
    if (bar.type != PCI_BAR_MEMORY || !bar.address || !bar.size)
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
    return true;
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

static bool SATA_stop_engine(volatile ahci_port *port, TimerSource &timer) {
     // 1. Stop command processing.
    port->CMD &= ~CMD_ST;

    // 2. Wait until command-list DMA engine stops.
    bool command_stopped = false;

    for (u32 elapsed = 0; elapsed < 1000; elapsed++) {
        if (!(port->CMD & CMD_CR)) {
            command_stopped = true;
            break;
        }

        wait_ms(timer, 1);
    }

    if (!command_stopped)
        return false;

    // 3. Stop FIS reception.
    port->CMD &= ~CMD_FRE;

    // 4. Wait until FIS receive DMA engine stops.
    for (u32 elapsed = 0; elapsed < 1000; elapsed++) {
        if (!(port->CMD & CMD_FR))
            return true;

        wait_ms(timer, 1);
    }

    return false;
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
    if (!(id[49] & (1 << 9)))
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

#define ATA_READ_DMA_EXT  0x25
#define ATA_WRITE_DMA_EXT 0x35

#define ATA_MAX_TRANSFER 0x400000

static bool ATA_transfer(
    u32 port,
    volatile ahci_port *port_reg,
    u64 lba,
    u16 sector_count,
    void *data,
    bool write,
    TimerSource &timer
) {
    if (!data || !sector_count)
        return false;

    if (!ports[port].initialized ||
        ports[port].device_type != ATA)
        return false;

    u32 sector_size = ports[port].sector_size;

    if (!sector_size)
        return false;

    // Validate the requested LBA range.
    if (lba >= ports[port].capacity ||
        sector_count > ports[port].capacity - lba)
        return false;

    u64 byte_count = (u64)sector_count * sector_size;

    // This implementation uses one PRDT entry.
    if (byte_count > ATA_MAX_TRANSFER)
        return false;

    // A heap buffer can start within a page and span discontiguous frames.
    // A single PRDT entry requires an exact, contiguous physical range.
    dma_ret buffer = allocate_dma(byte_count);
    if (buffer.status != 0)
        return false;
    if (write)
        memcpy((void *)buffer.virtual_address, data, byte_count);

    dma_ret CMD_LIST = ports[port].Command_list;
    dma_ret CMD_TBL = ports[port].Command_Table;

    auto *header =
        (AHCI_command_header *)CMD_LIST.virtual_address;

    auto *cmd_table =
        (AHCI_command_table *)CMD_TBL.virtual_address;

    // Prepare command header.
    memset(header, 0, sizeof(*header));

    header->flags = 5;

    if (write)
        header->flags |= AHCI_CMD_W;

    header->prdtl = 1;
    header->prdbc = 0;

    u64 table_phys = CMD_TBL.physical_address;

    header->ctba = (u32)table_phys;
    header->ctbau = (u32)(table_phys >> 32);

    // Prepare command table.
    memset(cmd_table, 0, 144);

    // Build Register Host-to-Device FIS.
    u8 *fis = cmd_table->CFIS;

    fis[0] = 0x27;
    fis[1] = 1u << 7;

    fis[2] = write
        ? ATA_WRITE_DMA_EXT
        : ATA_READ_DMA_EXT;

    // 48-bit LBA.
    fis[4] = (u8)lba;
    fis[5] = (u8)(lba >> 8);
    fis[6] = (u8)(lba >> 16);

    // Enable LBA mode.
    fis[7] = 1u << 6;

    fis[8]  = (u8)(lba >> 24);
    fis[9]  = (u8)(lba >> 32);
    fis[10] = (u8)(lba >> 40);

    // Sector count (16-bit).
    fis[12] = (u8)sector_count;
    fis[13] = (u8)(sector_count >> 8);

    // Configure PRDT.
    u64 buffer_phys = buffer.physical_address;

    cmd_table->PRDT[0].DBA = (u32)buffer_phys;
    cmd_table->PRDT[0].DBAU = (u32)(buffer_phys >> 32);
    cmd_table->PRDT[0].reserved = 0;
    cmd_table->PRDT[0].DBC = (u32)(byte_count - 1);

    // Clear previous errors.
    port_reg->IS = 0xFFFFFFFF;
    port_reg->SERR = 0xFFFFFFFF;

    // Make DMA structures visible before submission.
    asm volatile("mfence" ::: "memory");

    // Submit command through slot 0.
    if (!ATA_issue_command(port_reg, 0, timer)) {
        printf("ATA: transfer failed on port %u\n", port);
        free_dma(buffer.virtual_address);
        return false;
    }

    // Ensure DMA writes are visible to the CPU.
    asm volatile("mfence" ::: "memory");

    if (header->prdbc != byte_count) {
        printf("ATA: incomplete transfer: %u / %u bytes\n",
            header->prdbc, (u32)byte_count);

        free_dma(buffer.virtual_address);
        return false;
    }

    if (!write)
        memcpy(data, (void *)buffer.virtual_address, byte_count);
    free_dma(buffer.virtual_address);
    return true;
}

static bool ATA_read_sectors(port_descriptor *port, u32 lba, u32 sector_count, void *buffer) {
    Timer *timer = (Timer *)timeradd;
    TimerSource &timer_source = *timer->Src;

    if (!port || !buffer || sector_count == 0 || port->sector_size == 0)
        return false;

    u64 byte_count = u64(sector_count) * 512;
    u64 byte_start = u64(lba) * 512;
    u32 lba_remainder = byte_start % port->sector_size;
    u64 covered_bytes = lba_remainder + byte_count;
    u64 blocks = covered_bytes / port->sector_size
        + (covered_bytes % port->sector_size != 0);
    u64 first_block = byte_start / port->sector_size;
    if (first_block > 0xFFFFFFFFULL ||
        blocks > 0x100000000ULL - first_block)
        return false;
    lba = static_cast<u32>(first_block);

    void *temp = kmalloc(blocks * port->sector_size);
    if (!temp)
        return false;

    void *original_temp = temp;
    
    volatile ahci_port *port_reg = (volatile ahci_port *)port_to_address(port->port_number);

    u32 iterations = blocks / 0xFFFF;
    u32 remainder = blocks % 0xFFFF;

    if (remainder != 0) {
        ATA_transfer(port->port_number, port_reg, (u64)lba, remainder, temp, false, timer_source);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * port->sector_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ATA_transfer(port->port_number, port_reg, (u64)lba, 0xFFFF, temp, false, timer_source);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * port->sector_size));
    }

    memcpy(buffer, static_cast<u8 *>(original_temp) + lba_remainder, byte_count);
    kfree(original_temp);

   return true;
}

static inline void temp_read(port_descriptor *port, u32 lba, u32 iterations, u32 remainder, void *temp, TimerSource &timer, volatile ahci_port *port_reg) {
    if (remainder != 0) {
        ATA_transfer(port->port_number, port_reg, (u64)lba, remainder, temp, false, timer);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * port->sector_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ATA_transfer(port->port_number, port_reg, (u64)lba, remainder, temp, false, timer);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * port->sector_size));
    }

}


static bool ATA_write_sectors(port_descriptor *port, u32 lba, u32 sector_count, void *buffer) {
    Timer *timer = (Timer *)timeradd;
    TimerSource &timer_source = *timer->Src;

    volatile ahci_port *port_reg = (volatile ahci_port *)port_to_address(port->port_number);

    if (!port || !buffer || sector_count == 0 || port->sector_size == 0)
        return false;


    u64 byte_count = u64(sector_count) * 512;
    u64 byte_start = u64(lba) * 512;
    u32 lba_remainder = byte_start % port->sector_size;
    u64 covered_bytes = lba_remainder + byte_count;
    u64 blocks = covered_bytes / port->sector_size
        + (covered_bytes % port->sector_size != 0);
    u64 first_block = byte_start / port->sector_size;
    if (first_block > 0xFFFFFFFFULL ||
        blocks > 0x100000000ULL - first_block)
        return false;
    lba = static_cast<u32>(first_block);

    void *temp = kmalloc(blocks * port->sector_size);
    if (!temp)
        return false;
    
    void *original_temp = temp;
    
    u32 iterations = blocks / 0xFFFF;
    u32 remainder = blocks % 0xFFFF;

    u64 RemSize = 0;
    u64 ata_size = blocks * port->sector_size;
    u64 legacy_size = sector_count << 9;
    if (ata_size > legacy_size)
        RemSize = ata_size - legacy_size;

    if (RemSize) {
        temp_read(port, (u32)lba, iterations, remainder, temp, timer_source, port_reg);
    }

    memcpy(static_cast<u8 *>(temp) + lba_remainder, buffer, byte_count);

    if (remainder != 0) {
        ATA_transfer(port->port_number, port_reg, (u64)lba, remainder, temp, true, timer_source);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * port->sector_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ATA_transfer(port->port_number, port_reg, (u64)lba, 0xFFFF, temp, true, timer_source);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * port->sector_size));
    }

    kfree(original_temp);

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

static bool ATAPI_SCSI(u32 port, volatile ahci_port *port_reg, const u8 *cdb, u32 cdb_length, void *buffer, u32 data_length, bool host_to_device, TimerSource &timer) {
    if (!data_length)
        return ATAPI_packet(port, port_reg, cdb, cdb_length, nullptr, 0, host_to_device, timer);
    if (!buffer)
        return false;
    dma_ret dma = allocate_dma(data_length);
    if (dma.status != 0)
        return false;
    if (host_to_device)
        memcpy((void *)dma.virtual_address, buffer, data_length);
    bool ok = ATAPI_packet(port, port_reg, cdb, cdb_length, &dma, data_length, host_to_device, timer);
    asm volatile("mfence" ::: "memory");
    if (ok && !host_to_device)
        memcpy(buffer, (void *)dma.virtual_address, data_length);
    free_dma(dma.virtual_address);
    return ok;
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


static void ATAPI_read(port_descriptor *port, u32 lba, u16 sector_count, void *buffer) {
    volatile ahci_port *port_reg = (volatile ahci_port *)port_to_address(port->port_number);
    Timer *timer = (Timer *)timeradd;
    TimerSource &timer_source = *timer->Src;



    u8 cdb[10] = {
        0x28, // READ(10)
        0x00,
        (lba >> 24), (lba >> 16), (lba >> 8), lba, // LBA = 0
        0x00,
        (sector_count>>8), sector_count,             
        0x00
    };


    ATAPI_SCSI(port->port_number, port_reg, cdb, 10, buffer, (sector_count*port->sector_size), false, timer_source);
}


static void ATAPI_write(port_descriptor *port, u32 lba, u16 sector_count, void *buffer) {
    volatile ahci_port *port_reg = (volatile ahci_port *)port_to_address(port->port_number);
    Timer *timer = (Timer *)timeradd;
    TimerSource &timer_source = *timer->Src;


    u8 cdb[10] = {
        0x2A, // WRITE(10)
        0x00,
        (lba >> 24), (lba >> 16), (lba >> 8), lba, // LBA = 0
        0x00,
        (sector_count>>8), sector_count,             
        0x00
    };

    ATAPI_SCSI(port->port_number, port_reg, cdb, 10, buffer, (sector_count*port->sector_size), true, timer_source);
}

static inline void atapi_temp_read(port_descriptor *port, u32 lba, u32 iterations, u32 remainder, void *temp) {
    if (remainder != 0) {
        ATAPI_read(port, lba, remainder, temp);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * port->sector_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ATAPI_read(port, lba, 0xFFFF, temp);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * port->sector_size));
    }

}


static void ATAPI_Read_sectors(port_descriptor *port, u32 lba, u32 sector_count, void *buffer) {
    if (!port || !buffer || sector_count == 0 || port->sector_size == 0)
        return;

    u64 byte_count = u64(sector_count) * 512;
    u64 byte_start = u64(lba) * 512;
    u32 lba_remainder = byte_start % port->sector_size;
    u64 covered_bytes = lba_remainder + byte_count;
    u64 blocks = covered_bytes / port->sector_size
        + (covered_bytes % port->sector_size != 0);
    u64 first_block = byte_start / port->sector_size;
    if (first_block > 0xFFFFFFFFULL ||
        blocks > 0x100000000ULL - first_block)
        return;
    lba = static_cast<u32>(first_block);

    void *temp = kmalloc(blocks * port->sector_size);
    if (!temp)
        return;

    void *original_temp = temp;
    

    u32 iterations = blocks / 0xFFFF;
    u32 remainder = blocks % 0xFFFF;

    if (remainder != 0) {
        ATAPI_read(port, lba, remainder, temp);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * port->sector_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ATAPI_read(port, lba, 0xFFFF, temp);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * port->sector_size));
    }

    memcpy(buffer, static_cast<u8 *>(original_temp) + lba_remainder, byte_count);
    kfree(original_temp);

}


static void ATAPI_Write_sectors(port_descriptor *port, u32 lba, u32 sector_count, void *buffer) {
    if (!port || !buffer || sector_count == 0 || port->sector_size == 0)
        return;


    u64 byte_count = u64(sector_count) * 512;
    u64 byte_start = u64(lba) * 512;
    u32 lba_remainder = byte_start % port->sector_size;
    u64 covered_bytes = lba_remainder + byte_count;
    u64 blocks = covered_bytes / port->sector_size
        + (covered_bytes % port->sector_size != 0);
    u64 first_block = byte_start / port->sector_size;
    if (first_block > 0xFFFFFFFFULL ||
        blocks > 0x100000000ULL - first_block)
        return;
    lba = static_cast<u32>(first_block);

    void *temp = kmalloc(blocks * port->sector_size);
    if (!temp)
        return;
    
    void *original_temp = temp;
    
    u32 iterations = blocks / 0xFFFF;
    u32 remainder = blocks % 0xFFFF;

    u64 RemSize = 0;
    u64 ata_size = blocks * port->sector_size;
    u64 legacy_size = sector_count << 9;
    if (ata_size > legacy_size)
        RemSize = ata_size - legacy_size;

    if (RemSize) {
        atapi_temp_read(port, (u32)lba, iterations, remainder, temp);
    }

    memcpy(static_cast<u8 *>(temp) + lba_remainder, buffer, byte_count);

    if (remainder != 0) {
        ATAPI_write(port, lba, remainder, temp);
        lba += remainder;
        temp = (void *)((u8 *)temp + (remainder * port->sector_size));
    }

    for (u32 i = 0; i < iterations; i++) {
        ATAPI_write(port, lba, 0xFFFF, temp);
        lba += 0xFFFF;
        temp = (void *)((u8 *)temp + (0xFFFF * port->sector_size));
    }

    kfree(original_temp);
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

    if (!SATA_stop_engine(port_reg, timer)) {
        printf("SATA: failed to stop engine\n");
        return false;
    }

    port_reg->FB = (u32)FIS.physical_address;
    port_reg->FBU = (u32)(FIS.physical_address >> 32);
    
    port_reg->CLB = (u32)CMD_LIST.physical_address;
    port_reg->CLBU = (u32)(CMD_LIST.physical_address >> 32);

    if (!SATA_start_engine(port_reg, timer)) {
        printf("SATA: failed to start engine\n");
        return false;
    }

    u32 det = port_reg->SSTS & 0xf;

    switch (det) {
        case 0: {
            ports[port] = {0};
            ports[port].device_present = false;
            break;
        }

        case 1: {
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

            port_reg->SERR = 0xFFFFFFFF;

            goto next;
        }

        case 2: {
            printf("SATA: incorrect det value for port 0x%b\n", port);
            return false;
        }

        case 3: {
            goto next;
        }

        case 4: {
            u32 sctl = port_reg->SCTL;
            if ((sctl & SCTL_DET_MASK) == SCTL_DET_OFF) {
                // software disabled the SATA PHY.
                u32 cmd = port_reg->CMD; // read the port's command register
                
                if (cmd & CMD_ST) {
                    port_reg->CMD = cmd & ~CMD_ST;

                    bool stopped = false;
                    for (u32 i = 0; i < 1000; i++) {// 1 second timeout
                        if (!(port_reg->CMD & CMD_CR)) {
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
        }
            
        default: {
            printf("SATA: incorrect det value for port 0x%b\n", port);
            return false;
        }
    }

    next:

    bool try_again = true;

    SigAgain:

    u32 sig = port_reg->SIG;

    if (sig == 0xFFFFFFFF) {
        // try reset
        if (try_again && issue_reset(port_reg, timer)) {
            try_again = false;
            goto SigAgain;
        }

        return false;
    } else {
        switch (sig) {
            case 0x00000101: {
                return ATA_identify(port, port_reg, timer);
            }
            
            case 0xEB140101: {
                // ATAPI device
                dma_ret ATAPI_dma = allocate_dma(4096);
                if (ATAPI_dma.status != 0)
                    return false;
                ports[port].data = ATAPI_dma;

                return ATAPI_init(port, port_reg, timer);
            }

            case 0xC33C0101: {
                return false;
            }

                

            case 0x96690101: {
                return false;
            }

            default: {
                return false;
            }

                
        }
    }
    
}

#define GHC_AE (1u << 31)
#define GHC_IE (1u << 1)
#define GHC_HR (1u << 0)

bool controller_init(TimerSource &timer) {
    if (!discover_ahci())
        return false;

    
    volatile ahci_cap_regs *cap_regs = (volatile ahci_cap_regs *)ahci.io_virt;

    u32 cap = cap_regs->CAP;
    port_count = (u32)(cap & CAP_NP); // get number of ports - 1
    port_count++;

    ports = new port_descriptor[port_count];

    cmd_slots = (u8)CAP_NCS(cap) + 1; // get number of command slots - 1
    addr_64 = (cap & CAP_S64A);  // get whether 64-bit dma addresses are supported

    if (!ahci_take_ownership(timer, cap_regs)) {
        printf("SATA: failed to obtain ownership\n");
        return false;
    }

    cap_regs->GHC &= ~GHC_IE;
    cap_regs->GHC |= (1u << 31); // enable ahci mode

    u32 implemented_ports = cap_regs->PI;

    for (u32 i = 0; i < port_count; i++) {
        if (!(implemented_ports & (1u << i)))
            continue;

        ports[i].connected = true;
        if (!ahci_enumrate_port(i, timer)) {
            printf("SATA: failed to initialize port %u\n", i);
            continue;
        }

        printf("SATA: initialized port %u\n", i);
        ports[i].port_number = i;
    }

    return true;
}


void SATA_drive::init() {
    drive_port = &ports[current_port];
    memcpy(vendor, "SATA", sizeof("SATA"));
    initialized = drive_port->initialized;
    current_port++;
}



void SATA_drive::read_sectors(u32 lba, u32 sector_count, void *buffer) {
    if (drive_port->device_type == ATA)
        ATA_read_sectors(drive_port, lba, sector_count, buffer);
    else if (drive_port->device_type == ATAPI)
        ATAPI_Read_sectors(drive_port, lba, sector_count, buffer);
    else
        printf("SATA: unknown device type\n");
} 




void SATA_drive::write_sectors(u32 lba, u32 sector_count, void *buffer) {
    if (drive_port->device_type == ATA)
        ATA_write_sectors(drive_port, lba, sector_count, buffer);
    else if (drive_port->device_type == ATAPI)
        ATAPI_Write_sectors(drive_port, lba, sector_count, buffer);
    else
        printf("SATA: unknown device type\n");
}
