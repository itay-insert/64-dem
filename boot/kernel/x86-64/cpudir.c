#include <stdint.h>
#include "uint_definitions.h"
#include "vga.h"
#include "cpuid.h"
#include "memory.h"


char buffer_vendor[30] = {0};
char buffer_brand[50] = {0};
u32 buffer_speed[4] = {0};
u32 buffer_model[3] = {0};
u32 buffer_cores[3] = {0};

void print_feature(char name[], int supported)
{
    printf(name);
    printf(": ");

    if (supported)
        Set_GlobalTextColor(Green);
    else
        Set_GlobalTextColor(Red);

    if (supported)
        printf("YES");
    else
        printf("NO");

    Set_GlobalTextColor(LightGray);
    printf("\n");
}

void cpu_info(void) {
    printf("\n");
    printf("cpu_info: \n");
    printf("\n");
    printf("\n");
    printf("Vendor: ");
    int ret = cpu_vendor((u64)&buffer_vendor);
    if (ret == 1) {
        printf("running on a virtual machine\n");
        vm_type((u64)&buffer_brand);
        printf("Vm_type: ");
        printf(buffer_brand);
        printf("\n");
        cpu_model((u64)&buffer_model);
        printf("Standard features: \n");

        u32 ecx = buffer_model[0];
        u32 edx = buffer_model[1];

        print_feature("FPU",       edx & (1<<0));
        print_feature("TSC",       edx & (1<<4));
        print_feature("MSR",       edx & (1<<5));
        print_feature("PAE",       edx & (1<<6));
        print_feature("APIC",      edx & (1<<9));
        print_feature("CMOV",      edx & (1<<15));
        print_feature("PAT",       edx & (1<<16));
        print_feature("PSE36",     edx & (1<<17));
        print_feature("CLFLUSH",   edx & (1<<19));
        print_feature("MMX",       edx & (1<<23));
        print_feature("FXSR",      edx & (1<<24));
        print_feature("SSE",       edx & (1<<25));
        print_feature("SSE2",      edx & (1<<26));
        print_feature("HTT",       edx & (1<<28));

        printf("\n");
        printf("Extended features: \n");

        print_feature("PCLMULQDQ", ecx & (1<<1));
        print_feature("VMX",       ecx & (1<<5));
        print_feature("FMA",       ecx & (1<<12));
        print_feature("CMPXCHG16B",ecx & (1<<13));
        print_feature("PCID",      ecx & (1<<17));
        print_feature("SSE4.1",    ecx & (1<<19));
        print_feature("SSE4.2",    ecx & (1<<20));
        print_feature("x2APIC",    ecx & (1<<21));
        print_feature("POPCNT",    ecx & (1<<23));
        print_feature("AES",       ecx & (1<<25));
        print_feature("XSAVE",     ecx & (1<<26));
        print_feature("OSXSAVE",   ecx & (1<<27));
        print_feature("AVX",       ecx & (1<<28));
        print_feature("F16C",      ecx & (1<<29));
        print_feature("RDRAND",    ecx & (1<<30));
        printf("\n");
    } else if (ret == 0) {
        printf(buffer_vendor);
        printf("\n");
        printf("Brand: ");
        cpu_brand((u64)&buffer_brand);
        printf(buffer_brand);
        printf("\n");
        u32 eax = cpu_model((u64)&buffer_model);
        u32 stepping = eax & 0xF;
        u32 model = (eax >> 4) & 0xF;
        u32 family = (eax >> 8) & 0xF;

        if (family == 0xF)
        {
            family += (eax >> 20) & 0xFF;
        }

        if (family == 0x6 || family == 0xf)
        model += ((eax >> 16) & 0xf) << 4;

        printf("Family: %u\n", family);
        printf("Model: %u\n", model);
        printf("Stepping: %u\n", stepping);
        printf("\n");
    
        int ret1 = cpu_cores((u64)&buffer_cores);
        if (ret1 == 0) {
            buffer_cores[2] = buffer_cores[1] / buffer_cores[0];
            printf("Cores: %u\n", buffer_cores[2]);
            printf("Logical proccessors: %u\n", buffer_cores[1]);
            printf("Threads per core: %u\n", buffer_cores[0]);
        } else if (ret1 == 1) {
            Set_GlobalTextColor(Red);
            printf("error: ");
            Set_GlobalTextColor(LightGray);
            printf("leaf not supported\n");
        } else if (ret1 == 2) {
            Set_GlobalTextColor(Red);
            printf("error: ");
            Set_GlobalTextColor(LightGray);
            printf("bug found at CPUID0H.0000000B\n");
            Set_GlobalTextColor(Cyan);
            printf("warning: this could affect future cpu functionality\n");
            Set_GlobalTextColor(LightGray);
        }

        ret1 = cpu_speed((u64)&buffer_speed);
        if (ret1 == 0) {
            printf("Base frequency: %u MHz\n", buffer_speed[0]);
            printf("Max frequency: %u MHz\n", buffer_speed[1]);
            printf("Bus frequency: %u MHz\n", buffer_speed[2]);
        } else if (ret1 == 1) {
            Set_GlobalTextColor(Red);
            printf("error: ");
            Set_GlobalTextColor(LightGray);
            printf("leaf not supported\n");
        }
        printf("\n");

        printf("Standard features: \n");

        u32 ecx = buffer_model[0];
        u32 edx = buffer_model[1];

        print_feature("FPU",       edx & (1<<0));
        print_feature("TSC",       edx & (1<<4));
        print_feature("MSR",       edx & (1<<5));
        print_feature("PAE",       edx & (1<<6));
        print_feature("APIC",      edx & (1<<9));
        print_feature("CMOV",      edx & (1<<15));
        print_feature("PAT",       edx & (1<<16));
        print_feature("PSE36",     edx & (1<<17));
        print_feature("CLFLUSH",   edx & (1<<19));
        print_feature("MMX",       edx & (1<<23));
        print_feature("FXSR",      edx & (1<<24));
        print_feature("SSE",       edx & (1<<25));
        print_feature("SSE2",      edx & (1<<26));
        print_feature("HTT",       edx & (1<<28));

        printf("\n");
        printf("Extended features: \n");

        print_feature("PCLMULQDQ", ecx & (1<<1));
        print_feature("VMX",       ecx & (1<<5));
        print_feature("FMA",       ecx & (1<<12));
        print_feature("CMPXCHG16B",ecx & (1<<13));
        print_feature("PCID",      ecx & (1<<17));
        print_feature("SSE4.1",    ecx & (1<<19));
        print_feature("SSE4.2",    ecx & (1<<20));
        print_feature("x2APIC",    ecx & (1<<21));
        print_feature("POPCNT",    ecx & (1<<23));
        print_feature("AES",       ecx & (1<<25));
        print_feature("XSAVE",     ecx & (1<<26));
        print_feature("OSXSAVE",   ecx & (1<<27));
        print_feature("AVX",       ecx & (1<<28));
        print_feature("F16C",      ecx & (1<<29));
        print_feature("RDRAND",    ecx & (1<<30));
        printf("\n");

    }

   


}