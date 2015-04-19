#include "Ht.h"
#include "PersTest04.h"

void CPersTest04::PersTest04() {
	if (PR_htValid) {
		switch (PR_htInst) {
		case TEST04_ENTRY: {
			HtContinue(TEST04_WR);
			break;
		}
		case TEST04_WR: {
			PW_test04_0_0_data[0].write_addr(1);
			PW_test04_0_0_data[0] = (uint32_t)0x001f0506fc2d5be0LL;
			P_test04_0_0_data_RdAddr1 = (ht_uint1)1;
			HtContinue(TEST04_ST0);
			break;
		}
		case TEST04_ST0: {
			if (WriteMemBusy()) {
				HtRetry();
				break;
			}
			WriteMem_uint32_t(PR_memAddr + 0, PR_test04_0_0_data[0]);
			WriteMemPause(TEST04_LD0);
			break;
		}
		case TEST04_LD0: {
			if (ReadMemBusy()) {
				HtRetry();
				break;
			}
			ReadMem_test04_0_1_data(PR_memAddr + 0, 0, 1);
			P_test04_0_1_data_RdAddr1 = (ht_uint1)0;
			ReadMemPause(TEST04_CHK);
			break;
		}
		case TEST04_CHK: {
			if ((uint32_t)PR_test04_0_1_data != (uint32_t)0x001f0506fc2d5be0LL) {
				HtAssert(0, (uint32_t)0x00040000);
			}
			HtContinue(TEST04_RTN);
			break;
		}
		case TEST04_RTN: {
			if (SendReturnBusy_test04()) {
				HtRetry();
				break;
			}
			SendReturn_test04();
			break;
		}
		default:
			assert(0);
		}
	}
}
