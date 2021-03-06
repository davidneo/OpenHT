#include "Ht.h"
#include "PersInc9.h"

void
CPersInc9::PersInc9()
{
	if (PR_htValid) {
		switch (PR_htInst) {
		case INC9_INIT:
		{
			P_loopIdx = 0;

			P_wrGrpId = 0x1;

			HtContinue(INC9_READ);
		}
		break;
		case INC9_READ:
		{
			if (ReadMemBusy()) {
				HtRetry();
				break;
			}

			// Calculate memory read address
			MemAddr_t memRdAddr = SR_arrayAddr + ((P_loopBase + P_loopIdx) << 3);

			Inc9Ptr_t arrayMemWrPtr = (Inc9Ptr_t)P_loopIdx;

			// Issue read request to memory
			ReadMem_arrayMem9(memRdAddr, arrayMemWrPtr);

			bool bLast = P_loopIdx == P_elemCnt - 1;
			if (bLast) {
				P_loopIdx = 0;
				ReadMemPause(INC9_WRITE);
			} else {
				P_loopIdx += 1;
				HtContinue(INC9_READ);
			}

			// Set address for reading memory response data
			P_arrayMemRdPtr = (Inc9Ptr_t)P_loopIdx;
		}
		break;
		case INC9_WRITE:
		{
			//if (WriteMemBusy(rspGrpId)) {
			if (WriteMemBusy()) {
				//if (WriteMemBusy(wrRspGrpId)) {
				HtRetry();
				break;
			}

			// Increment memory data
			uint64_t memWrData = GR_arrayMem9.data + 1;

			// Calculate memory write address
			MemAddr_t memWrAddr = SR_arrayAddr + ((P_loopBase + P_loopIdx) << 3);

			// Issue write memory request
			//WriteMem(wrRspGrpId, memWrAddr, memWrData);
			WriteMem(PR_wrGrpId, memWrAddr, memWrData);

			bool bLast = P_loopIdx == P_elemCnt - 1;
			if (bLast) {
				P_loopIdx = 0;
				//WriteMemPause(wrRspGrpId, INC_RTN);
				WriteMemPause(PR_wrGrpId, INC9_RTN);
				//HtPause();
			} else {
				P_loopIdx += 1;
				HtContinue(INC9_WRITE);
			}

			// Set address for reading memory response data
			P_arrayMemRdPtr = (Inc9Ptr_t)P_loopIdx;
		}
		break;
		case INC9_RTN:
		{
			if (SendReturnBusy_inc9()) {
				HtRetry();
				break;
			}

			SendReturn_inc9(P_elemCnt);

			break;
		}
		default:
			assert(0);
		}
	}
}
