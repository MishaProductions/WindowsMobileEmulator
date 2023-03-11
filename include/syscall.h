/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef SYSCALL__H_
#define SYSCALL__H_

class CSystemCallbacks;

class CSystem
{
public:
	typedef unsigned __int32 SYSTEM_ADDRESS;
	typedef unsigned __int32 SYSTEM_REGISTER;

	void SystemCall(CSystemCallbacks*);

	enum CpuId
	{
		ARM,
		MIPS,
		SH
	};
};

class CSystemCallbacks
{
public:
	virtual bool					 SyscallReadMemory   (	  void*  pvDst, CSystem::SYSTEM_ADDRESS addr, size_t size)   = 0;
	virtual bool					 SyscallWriteMemory  (const void*  pvSrc, CSystem::SYSTEM_ADDRESS addr, size_t size) = 0;
	virtual CSystem::SYSTEM_REGISTER SyscallReadRegister (unsigned int idReg)											 = 0;
	virtual void					 SyscallWriteRegister(unsigned int idReg, CSystem::SYSTEM_REGISTER regValue)		 = 0;
};

#endif //SYSCALL__H_
