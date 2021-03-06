#include "Ht.h"
#include "PersTest00.h"

void CPersTest00::PersTest00() {
	if (PR1_htValid) {
		switch (PR1_htInst) {
		case TEST00_ENTRY: {
			HtContinue(TEST00_WR);
			break;
		}
		case TEST00_WR: {
			HtContinue(TEST00_ST0);
			break;
		}
		case TEST00_ST0: {
			if (WriteMemBusy()) {
				HtRetry();
				break;
			}
			WriteMem_test00_0_src_u0_data(PR1_memAddr + 0, 6, 0, 1);
			WriteMemPause(TEST00_LD0);
			break;
		}
		case TEST00_LD0: {
			if (ReadMemBusy()) {
				HtRetry();
				break;
			}
			ReadMem_test00_0_src_u0_data(PR1_memAddr + 0, 0, 0, 1);
			ReadMemPause(TEST00_CHK);
			break;
		}
		case TEST00_CHK: {
			if ((ht_uint8)PR1_test00_0_src_u0_data[0].test00_0_src_v0_data[0] != ((ht_uint8)0x0000000000000060LL)) {
				HtAssert(0, 0);
			}
			if ((ht_int64)PR1_test00_0_src_u0_data[0].test00_0_src_v1_data != ((ht_int64)0x0015d74eb55d5960LL)) {
				printf("EXP - v1 = 0x%016lx\n", (int64_t)0x0015d74eb55d5960LL);
				printf("ACT - v1 = 0x%016lx\n", (int64_t)PR1_test00_0_src_u0_data[0].test00_0_src_v1_data);
				HtAssert(0, 0);
			}
			HtContinue(TEST00_RTN);
			break;
		}
		case TEST00_RTN: {
			if (SendReturnBusy_test00()) {
				HtRetry();
				break;
			}
			SendReturn_test00();
			break;
		}
		default:
			assert(0);
		}
	}
	if (PR5_htValid) {
		switch (PR5_htInst) {
		case TEST00_ENTRY: {
			break;
		}
		case TEST00_WR: {
			PW5_test00_0_src_u0_data[0].write_addr(6);
			PW5_test00_0_src_u0_data[0].test00_0_src_v1_data = ((int64_t)0x0015d74eb55d5960LL);
			break;
		}
		case TEST00_ST0: {
			break;
		}
		case TEST00_LD0: {
			P5_test00_0_src_u0_data_RdAddr1 = (ht_uint3)0;
			break;
		}
		case TEST00_CHK: {
			break;
		}
		case TEST00_RTN: {
			break;
		}
		default:
			assert(0);
		}
	}
}
