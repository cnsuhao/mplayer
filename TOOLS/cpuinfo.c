/* small utility to extract CPU information
 Used by configure to set CPU optimization levels on some operating
 systems where /proc/cpuinfo is non-existent or unreliable. */

#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#ifdef __MINGW32__
#include <sys/timeb.h>
void gettimeofday(struct timeval* t,void* timezone)
{       struct timeb timebuffer;
        ftime( &timebuffer );
        t->tv_sec=timebuffer.time;
        t->tv_usec=1000*timebuffer.millitm;
}
#define MISSING_USLEEP
#define sleep(t) _sleep(1000*t);
#endif

#ifdef __BEOS__
#define usleep(t) snooze(t)
#endif

#ifdef M_UNIX
typedef long long int64_t;
#define	MISSING_USLEEP
#else
#include <inttypes.h>
#endif


typedef struct cpuid_regs {
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
} cpuid_regs_t;

static cpuid_regs_t
cpuid(int func) {
	cpuid_regs_t regs;
#define	CPUID	".byte 0x0f, 0xa2; "
	asm("push %%ebx; "
	    "movl %4,%%eax; " CPUID
	    "movl %%eax,%0; movl %%ebx,%1; movl %%ecx,%2; movl %%edx,%3; "
	    "pop %%ebx"
		: "=m" (regs.eax), "=m" (regs.ebx), "=m" (regs.ecx), "=m" (regs.edx)
		: "g" (func)
		: "%eax", "%ecx", "%edx");
	return regs;
}


static int64_t
rdtsc(void)
{
	unsigned int i, j;
#define	RDTSC	".byte 0x0f, 0x31; "
	asm(RDTSC : "=a"(i), "=d"(j) : );
	return ((int64_t)j<<32) + (int64_t)i;
}


static void
store32(char *d, unsigned int v)
{
	d[0] =  v        & 0xff;
	d[1] = (v >>  8) & 0xff;
	d[2] = (v >> 16) & 0xff;
	d[3] = (v >> 24) & 0xff;
}


int
main(int argc, char **argv)
{
	cpuid_regs_t regs, regs_ext;
	char idstr[13];
	unsigned max_cpuid;
	unsigned max_ext_cpuid;
	unsigned int amd_flags;
	unsigned int amd_flags2;
	char *model_name = "Unknown CPU";
	int i;
	char processor_name[49];

	regs = cpuid(0);
	max_cpuid = regs.eax;
	/* printf("%d CPUID function codes\n", max_cpuid+1); */

	store32(idstr+0, regs.ebx);
	store32(idstr+4, regs.edx);
	store32(idstr+8, regs.ecx);
	idstr[12] = 0;
	printf("vendor_id\t: %s\n", idstr); 

	if (strcmp(idstr, "GenuineIntel") == 0)
	    model_name = "Unknown Intel CPU";
	else if (strcmp(idstr, "AuthenticAMD") == 0)
	    model_name = "Unknown AMD CPU";

	regs_ext = cpuid((1<<31) + 0);
	max_ext_cpuid = regs_ext.eax;
	if (max_ext_cpuid >= (1<<31) + 1) {
	    regs_ext = cpuid((1<<31) + 1);
	    amd_flags = regs_ext.edx;
	    amd_flags2 = regs_ext.ecx;

	    if (max_ext_cpuid >= (1<<31) + 4) {
		for (i = 2; i <= 4; i++) {
		    regs_ext = cpuid((1<<31) + i);
		    store32(processor_name + (i-2)*16, regs_ext.eax);
		    store32(processor_name + (i-2)*16 + 4, regs_ext.ebx);
		    store32(processor_name + (i-2)*16 + 8, regs_ext.ecx);
		    store32(processor_name + (i-2)*16 + 12, regs_ext.edx);
		}
		processor_name[48] = 0;
		model_name = processor_name;
	    }
	} else {
	    amd_flags = 0;
	    amd_flags2 = 0;
	}

	if (max_cpuid >= 1) {
		static struct {
			int bit;
			char *desc;;
			char *description;
		} cap[] = {
			{ 0,  "fpu",   "Floating-point unit on-chip" },
			{ 1,  "vme",   "Virtual Mode Enhancements" },
			{ 2,  "de",    "Debugging Extension" },
			{ 3,  "pse",   "Page Size Extension" },
			{ 4,  "tsc",   "Time Stamp Counter" },
			{ 5,  "msr",   "Pentium Processor MSR" },
			{ 6,  "pae",   "Physical Address Extension" },
			{ 7,  "mce",   "Machine Check Exception" },
			{ 8,  "cx8",   "CMPXCHG8B Instruction Supported" },
			{ 9,  "apic",  "On-chip CPIC Hardware Enabled" },
			{ 11, "sep",   "SYSENTER and SYSEXIT" },
			{ 12, "mtrr",  "Memory Type Range Registers" },
			{ 13, "pge",   "PTE Global Bit" },
			{ 14, "mca",   "Machine Check Architecture" },
			{ 15, "cmov",  "Conditional Move/Compare Instruction" },
			{ 16, "pat",   "Page Attribute Table" },
			{ 17, "pse36", "Page Size Extension 36-bit" },
			{ 18, "psn",   "Processor Serial Number" },
			{ 19, "cflsh", "CFLUSH instruction" },
			{ 21, "ds",    "Debug Store" },
			{ 22, "acpi",  "Thermal Monitor and Clock Ctrl" },
			{ 23, "mmx",   "MMX Technology" },
			{ 24, "fxsr",  "FXSAVE/FXRSTOR" },
			{ 25, "sse",   "SSE Extensions" },
			{ 26, "sse2",  "SSE2 Extensions" },
			{ 27, "ss",    "Self Snoop" },
			{ 28, "htt",   "Multi-threading" },
			{ 29, "tm",    "Therm. Monitor" },
			{ 30, "ia64",  "IA-64 Processor" },
			{ 31, "pbe",   "Pend. Brk. EN." },
			{ -1 }
		};
		static struct {
			int bit;
			char *desc;
			char *description;
		} cap2[] = {
			{ 0, "sse3", "SSE3 Extensions" },
			{ 3, "monitor", "MONITOR/MWAIT" },
			{ 4, "ds-cpl", "CPL Qualified Debug Store" },
			{ 5, "vmx", "Virtual Machine Extensions" },
			{ 7, "est", "Enhanced Intel SpeedStep Technology" },
			{ 8, "tm2", "Thermal Monitor 2" },
			{ 10, "cnxt-id", "L1 Context ID" },
			{ 13, "cmpxchg16b", "CMPXCHG16B Available" },
			{ -1 }
		};
		static struct {
			int bit;
			char *desc;;
			char *description;
		} cap_amd[] = {
			{ 11, "syscall", "SYSCALL and SYSRET" },
			{ 19, "mp", "MP Capable" },
			{ 20, "nx", "No-Execute Page Protection" },
			{ 22, "mmxext","MMX Technology (AMD Extensions)" },
			{ 25, "fxsr_opt", "Fast FXSAVE/FXRSTOR" },
			{ 27, "rdtscp", "RDTSCP Instruction" },
			{ 30, "3dnowext","3Dnow! Extensions" },
			{ 31, "3dnow", "3Dnow!" },
			{ -1 }
		};
		static struct {
			int bit;
			char *desc;
			char *description;
		} cap_amd2[] = {
			{ 0, "lahf_lm", "LAHF/SAHF Supported in 64-bit Mode" },
			{ 1, "cmp_legacy", "Chip Multi-Core" },
			{ 2, "svm", "Secure Virtual Machine" },
			{ 4, "cr8", "CR8 Available in Legacy Mode" },
			{ -1 }
		};
		unsigned int family, model, stepping;

		regs = cpuid(1);
		printf("cpu family\t: %d\n"
		       "model\t\t: %d\n"
		       "stepping\t: %d\n" ,
			family = (regs.eax >> 8) & 0xf,
			model = (regs.eax >> 4) & 0xf,
			stepping = regs.eax & 0xf);
		
		printf("flags\t\t:");
		for (i = 0; cap[i].bit >= 0; i++) {
			if (regs.edx & (1 << cap[i].bit)) {
				printf(" %s", cap[i].desc);
			}
		}
		for (i = 0; cap2[i].bit >= 0; i++) {
			if (regs.ecx & (1 << cap[i].bit)) {
				printf(" %s", cap2[i].desc);
			}
		}
		/* k6_mtrr is supported by some AMD K6-2/K6-III CPUs but
		   it is not indicated by a CPUID feature bit, so we
		   have to check the family, model and stepping instead. */
		if (strstr(idstr, "AMD") &&
		   	family == 5 && 
			(model >= 9 || model == 8 && stepping >= 8))
			printf(" %s", "k6_mtrr");

		for (i = 0; cap_amd[i].bit >= 0; i++) {
			if (amd_flags & (1 << cap_amd[i].bit)) {
				printf(" %s", cap_amd[i].desc);
			}
		}
		for (i = 0; cap_amd2[i].bit >= 0; i++) {
			if (amd_flags2 & (1 << cap_amd2[i].bit)) {
				printf(" %s", cap_amd2[i].desc);
			}
		}
		printf("\n");

		if (regs.edx & (1 << 4)) {
			int64_t tsc_start, tsc_end;
			struct timeval tv_start, tv_end;
			int usec_delay;

			tsc_start = rdtsc();
			gettimeofday(&tv_start, NULL);
#ifdef	MISSING_USLEEP
			sleep(1);
#else
			usleep(100000);
#endif
			tsc_end = rdtsc();
			gettimeofday(&tv_end, NULL);

			usec_delay = 1000000 * (tv_end.tv_sec - tv_start.tv_sec)
				+ (tv_end.tv_usec - tv_start.tv_usec);

			printf("cpu MHz\t\t: %.3f\n", 
				(double)(tsc_end-tsc_start) / usec_delay);
		}
	}

	printf("model name\t: %s\n", model_name);

	exit(0);
}
