/*
 * runtime library for profiling
 * Jun 2015 Tong Zhang<ztong@vt.edu>
 */

#include <stdlib.h>


extern "C" void TxProfFini()
{
}

extern "C" void TxProfInit()
{
  atexit(TxProfFini);
}

extern "C" void TxProfBeforeCall(unsigned InsID)
{
}

extern "C" void TxProfAfterCall(unsigned InsID)
{
}

extern "C" void TxProfEnter(unsigned FuncID)
{
}

extern "C" void TxProfExit(unsigned FuncID)
{
}

