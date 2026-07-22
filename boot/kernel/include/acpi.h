#include "uint_definitions.h"
#ifndef ACPI_H
#define ACPI_H


typedef struct {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
} __attribute__((packed)) RSDPDescriptor20;


typedef struct {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} __attribute__((packed)) ACPISDTHeader;


typedef struct {
	u8 address_space_id;
	u8 register_bit_width;
	u8 register_bit_offset;
	u8 access_size;
	u64 address;
} __attribute__((packed)) ACPIAddress;
	

typedef struct {
    ACPISDTHeader header;

    u32 firmware_control;
    u32 dsdt;

    u8 reserved0;
    u8 preferred_pm_profile;
    u16 sci_interrupt;
    u32 smi_command_port;

    u8 acpi_enable;
    u8 acpi_disable;
    u8 s4bios_request;
    u8 pstate_control;

    u32 pm1a_event_block;             
    u32 pm1b_event_block;             
    u32 pm1a_control_block;           
    u32 pm1b_control_block;           
    u32 pm2_control_block;            
    u32 pm_timer_block;               

    u32 gpe0_block;                   
    u32 gpe1_block;                   

    u8 pm1_event_length;              
    u8 pm1_control_length;            
    u8 pm2_control_length;            
    u8 pm_timer_length;               
    u8 gpe0_length;                 
    u8 gpe1_length;                   
    u8 gpe1_base;                     
    u8 cstate_control;                

    u16 worst_c2_latency;            
    u16 worst_c3_latency;             
    u16 flush_size;                   
    u16 flush_stride;                 

    u8 duty_offset;                   
    u8 duty_width;                    
    u8 day_alarm;                    
    u8 month_alarm;                   
    u8 century;                       

    u16 ia_pc_boot_architecture;      
    u8 reserved1;                     
    u32 flags;                        

    ACPIAddress reset_register;       
    u8 reset_value;                   

    u16 arm_boot_architecture;        
    u8 fadt_minor_version;            

    u64 x_firmware_control;           
    u64 x_dsdt;                       

    ACPIAddress x_pm1a_event_block;   
    ACPIAddress x_pm1b_event_block;   
    ACPIAddress x_pm1a_control_block; 
    ACPIAddress x_pm1b_control_block;
    ACPIAddress x_pm2_control_block;  
    ACPIAddress x_pm_timer_block;     

    ACPIAddress x_gpe0_block;         
    ACPIAddress x_gpe1_block;         

} __attribute__((packed)) FADT;

typedef struct {
    ACPISDTHeader header;
    u32 EventTimerBlockID;
    ACPIAddress Address;
    u8 HPETNumber;
    u16 MinimumTick;
    u8 PageProtection;
} __attribute__((packed)) ACPI_HPET;


typedef struct {
    ACPISDTHeader header;

    u32 lapic_address;
    u32 flags;

    u8 entries[];
} __attribute__((packed)) MADT;


typedef struct {
    u8 type;
    u8 length;
} __attribute__((packed)) MADTEntryHeader;


typedef struct {
    u8 type;
    u8 length;

    u8 ioapic_id;
    u8 reserved;

    u32 address;
    u32 gsi_base;
} __attribute__((packed)) MADTIOAPIC;


typedef struct {
    u64 io_base;
    int code;
} PM_ret;

typedef struct {
    char signature[4];
    u64 Address;
    PM_ret simple_timer;
    int status;
} ACPI_ret;

u64 find_rsdp_legacy(void);
void rsdp_init(u64 rsdp_base);
ACPI_ret ACPI_discovery(const char *signature);

#endif