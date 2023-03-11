/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef __PLACE_H
#define __PLACE_H

const unsigned __int8 EAX_Reg=0;
const unsigned __int8 ECX_Reg=1;
const unsigned __int8 EDX_Reg=2;
const unsigned __int8 EBX_Reg=3;
const unsigned __int8 ESP_Reg=4;
const unsigned __int8 EBP_Reg=5;
const unsigned __int8 ESI_Reg=6;
const unsigned __int8 EDI_Reg=7;

const unsigned __int8 AL_Reg=EAX_Reg;
const unsigned __int8 CL_Reg=ECX_Reg;
const unsigned __int8 DL_Reg=EDX_Reg;
const unsigned __int8 BL_Reg=EBX_Reg;
const unsigned __int8 AH_Reg=4;
const unsigned __int8 CH_Reg=5;
const unsigned __int8 DH_Reg=6;
const unsigned __int8 BH_Reg=7;

const unsigned __int8 MM0_Reg=0;
const unsigned __int8 MM1_Reg=1;
const unsigned __int8 MM2_Reg=2;
const unsigned __int8 MM3_Reg=3;
const unsigned __int8 MM4_Reg=4;
const unsigned __int8 MM5_Reg=5;
const unsigned __int8 MM6_Reg=6;
const unsigned __int8 MM7_Reg=7;
typedef unsigned __int8 IntelReg;

#define Emit8(b) \
{ \
	*CodeLocation = (b); \
    CodeLocation++; \
}

#define Emit16(h) \
{ \
    *(unsigned __int16*)CodeLocation = (h); \
	CodeLocation+=2; \
}

#define Emit32(w) \
{ \
	*(unsigned __int32*)CodeLocation = (w); \
	CodeLocation+=4; \
}

#define EmitPtr(p) Emit32(PtrToLong(p))

#define EmitStruct(ps) \
{ \
	memcpy(CodeLocation, ps, sizeof(*ps)); \
    CodeLocation+=sizeof(*ps); \
}

#define EmitRel32(pfn) Emit32(PtrToLong((size_t)(pfn) - (size_t)CodeLocation) - 4)

#define Emit_SIZE16()  Emit8(0x66);

#define EmitModRmReg(m, rm, reg) Emit8( ((m) << 6) | ((reg)<<3) | (rm))
#define EmitSIB(ss, index, base) Emit8( ((ss) << 6) | ((index) << 3) | (base))

// Generates "MOV Reg, DWORD PTR Ptr"
#define Emit_MOV_Reg_DWORDPTR(Reg, Ptr) \
{ \
	if (Reg == EAX_Reg) { \
		Emit8(0xa1); EmitPtr(Ptr); \
	} else { \
		Emit8(0x8b); EmitModRmReg(0,5,Reg); EmitPtr(Ptr); \
	} \
}

// Generates "MOV REG BYTE PTR Ptr"
#define Emit_MOV_Reg_BYTEPTR(Reg, Ptr) \
{ \
	if (Reg == AL_Reg) { \
		Emit8(0xa0); EmitPtr(Ptr); \
	} else { \
		Emit8(0x8a); EmitModRmReg(0,5,Reg); EmitPtr(Ptr); \
	} \
}

#define Emit_MOV_DWORDPTR_Reg(Ptr, Reg) \
{ \
	if (Reg == EAX_Reg) { \
		Emit8(0xa3); EmitPtr(Ptr); \
	} else { \
		Emit8(0x89); EmitModRmReg(0,5,Reg); EmitPtr(Ptr); \
	} \
}

#define Emit_MOV_Reg_Imm32(Reg, Imm32) { Emit8(0xb8+Reg); Emit32(Imm32); }

#define Emit_MOV_Reg_Reg(RegDest, RegSrc) { Emit8(0x8b); EmitModRmReg(3,RegSrc,RegDest); }

#define Emit_CALL(pfn) { Emit8(0xe8); EmitRel32(pfn); }
#define Emit_JMP32(pfn) { Emit8(0xe9); EmitRel32(pfn); }

#define Emit_RETN(Imm) \
{	\
	if (Imm) { \
		ASSERT(Imm >=0 && Imm <= 32767); \
		Emit8(0xc2); Emit16((unsigned __int16)Imm); \
	} else { \
		Emit8(0xc3); \
	} \
}

#define Emit_XOR_Reg_Reg(Reg1, Reg2) { Emit8(0x33); EmitModRmReg(3,Reg2,Reg1); }
#define Emit_CMC() Emit8(0xf5);

#define Emit_JZ(Offset8)  { Emit8(0x74); Emit8(Offset8); }
#define Emit_JZLabel(LabelName) { Emit_JZ(0); LabelName=CodeLocation; }
#define Emit_JNZ(Offset8) { Emit8(0x75); Emit8(Offset8); }
#define Emit_JNZLabel(LabelName) { Emit_JNZ(0); LabelName=CodeLocation; }
#define Emit_JB(Offset8)  { Emit8(0x72); Emit8(Offset8); }
#define Emit_JBLabel(LabelName)  { Emit_JB(0); LabelName=CodeLocation; }
#define Emit_JBE(Offset8) { Emit8(0x76); Emit8(Offset8); }
#define Emit_JBELabel(LabelName) { Emit_JBE(0); LabelName=CodeLocation; }
#define Emit_JA(Offset8)  { Emit8(0x77); Emit8(Offset8); }
#define Emit_JALabel(LabelName)  { Emit_JA(0); LabelName=CodeLocation; }
#define Emit_JNO(Offset8) { Emit8(0x71); Emit8(Offset8); }
#define Emit_JNOLabel(LabelName) { Emit_JNO(0); LabelName=CodeLocation; }
#define Emit_JNC(Offset8) { Emit8(0x73); Emit8(Offset8); }
#define Emit_JNCLabel(LabelName) { Emit_JNC(0); LabelName=CodeLocation; }
#define Emit_JCLabel(LabelName) Emit_JBLabel(LabelName)
#define Emit_JMP(Offset8) { Emit8(0xeb); Emit8(Offset8); }
#define Emit_JMPLabel(LabelName) { Emit_JMP(0); LabelName=CodeLocation; }
#define FixupLabel(LabelName) {ASSERT((size_t)(CodeLocation - LabelName) < 128); *(LabelName-1) = (unsigned __int8)(CodeLocation - LabelName); }

#define Emit_JZLabel32(LabelName) { Emit16(0x840f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JBLabel32(LabelName) { Emit16(0x820f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JMPLabel32(LabelName) { Emit8(0xe9); Emit32(0); LabelName=CodeLocation; }
#define FixupLabel32(LabelName) { *(unsigned __int32*)(LabelName-4) = (unsigned __int32)(CodeLocation - LabelName); }

// Always generate 32-bit conditional branch offsets
#define Emit_JZLabelFar(LabelName)  { Emit16(0x840f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JNZLabelFar(LabelName) { Emit16(0x850f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JCLabelFar(LabelName)  { Emit16(0x820f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JNCLabelFar(LabelName) { Emit16(0x830f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JNSLabelFar(LabelName) { Emit16(0x890f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JSLabelFar(LabelName) { Emit16(0x880f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JOLabelFar(LabelName) { Emit16(0x800f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JNOLabelFar(LabelName) { Emit16(0x810f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JLLabelFar(LabelName) { Emit16(0x8C0f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JGELabelFar(LabelName) { Emit16(0x8D0f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JLELabelFar(LabelName) { Emit16(0x8E0f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JGLabelFar(LabelName) { Emit16(0x8F0f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JBELabelFar(LabelName) { Emit16(0x860f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JAELabelFar(LabelName) { Emit16(0x830f); Emit32(0); LabelName=CodeLocation; }
#define Emit_JALabelFar(LabelName) { Emit16(0x870f); Emit32(0); LabelName=CodeLocation; }


#define FixupLabelFar(LabelName) { *(unsigned __int32*)(LabelName-4) = (unsigned __int32)(CodeLocation - LabelName); }

#define Emit_JMP_Reg(Reg); { Emit8(0xff); EmitModRmReg(3,Reg,4); }

#define Emit_PUSH_DWORDPTR(p) { Emit8(0xff); EmitModRmReg(0,5,6); EmitPtr(p); }
#define Emit_PUSH_Reg(Reg) Emit8(0x50+Reg);
#define Emit_PUSH32(Imm) { Emit8(0x68); Emit32(Imm); }
#define Emit_PUSHPtr(p)  { Emit8(0x68); EmitPtr(p); }
#define Emit_PUSH_M32(p); { Emit8(0xff); EmitModRmReg(0,5,6); EmitPtr(p); }

#define Emit_POP_Reg(Reg)  Emit8(0x58+Reg);
#define Emit_POP_M32(p); { Emit8(0x8f); EmitModRmReg(0,5,0); EmitPtr(p); }

#define Emit_SETC_Reg8(Reg) { Emit16(0x920f); EmitModRmReg(3,Reg,0); } // SETC

#define Emit_ADD_Reg32_Reg32(RegD,RegS) { Emit8(0x01); EmitModRmReg(3,RegD,RegS); } // ADD RegD,RegS
#define Emit_SUB_Reg32_Reg32(RegD,RegS) { Emit8(0x29); EmitModRmReg(3,RegD,RegS); } // SUB RegD,RegS
#define Emit_OR_Reg32_Reg32(RegD,RegS)  { Emit8(0x20); EmitModRmReg(3,RegD,RegS); } // OR RegD,RegS

#define Emit_ADD_Reg_Imm32(Reg, UnsignedImmValue) { \
    signed __int32 Imm = (signed __int32)(UnsignedImmValue); /* Cast to signed int to make optimization easier */ \
	if (Imm == 0) { \
		/* nothing needs to be done */ \
	} else if (Imm == 1) { \
		Emit_INC_Reg(Reg); /* INC Reg (1 byte) */ \
	} else if (Imm >= -128 && Imm <= 127) { \
		Emit8(0x83); EmitModRmReg(3,Reg,0); Emit8((unsigned __int8)Imm);  /* ADD Reg, Imm (3 bytes) */ \
	} else { \
		if (Reg == EAX_Reg) { \
			Emit8(0x05); Emit32(Imm); /* ADD EAX, Imm (5 bytes) */ \
		} else { \
			Emit8(0x81); EmitModRmReg(3,Reg,0); Emit32(Imm); /* ADD Reg, Imm (6 bytes) */ \
		} \
	} \
}

#define Emit_SUB_Reg_Imm32(Reg, /* unsigned */ UnsignedImmValue) { \
    signed __int32 Imm = (signed __int32)(UnsignedImmValue); /* Cast to signed int to make optimization easier */ \
	if (Imm == 0) { \
		/* nothing needs to be done */ \
	} else if (Imm == 1) { \
        Emit_DEC_Reg(Reg); /* DEC Reg (1 byte) */ \
	} else if (Imm >= -128 && Imm <= 127) { \
		Emit8(0x83); EmitModRmReg(3,Reg,5); Emit8((unsigned __int8)Imm);  /* ADD Reg, Imm (3 bytes) */ \
	} else { \
		if (Reg == EAX_Reg) { \
			Emit8(0x2d); Emit32(Imm); /* SUB EAX, Imm (5 bytes) */ \
		} else { \
			Emit8(0x81); EmitModRmReg(3,Reg,5); Emit32(Imm); /* SUB Reg, Imm (6 bytes) */ \
		} \
	} \
}

#define Emit_ADD_Reg_DWORDPTR(Reg,p); { Emit8(0x03); EmitModRmReg(0,5,Reg); EmitPtr(p); } // ADD Reg,D[p]
#define Emit_ADD_DWORDPTR_Reg(p,Reg); { Emit8(0x01); EmitModRmReg(0,5,Reg); EmitPtr(p); } // ADD D[p],Reg
#define Emit_ADC_DWORDPTR_Reg(p,Reg); { Emit8(0x11); EmitModRmReg(0,5,Reg); EmitPtr(p); } // ADC D[p],Reg

#define Emit_SUB_Reg_DWORDPTR(Reg,p); { Emit8(0x2b); EmitModRmReg(0,5,Reg); EmitPtr(p); } // SUB Reg,D[p]

#define Emit_AND_Reg_Imm32(Reg, Imm) { \
	if ((signed __int32)(Imm) >= -128 && (signed __int32)(Imm) < 128) { \
	    Emit8(0x83); EmitModRmReg(3,Reg,4); Emit8((unsigned __int8)(Imm)); \
	} else if (Reg == EAX_Reg) { \
		Emit8(0x25); Emit32(Imm); \
	} else { \
		Emit8(0x81); EmitModRmReg(3,Reg,4); Emit32(Imm); \
	} \
}

#define Emit_CMP_Reg_Imm32(Reg, Imm) { \
	if ((signed __int32)(Imm) >= -128 && (signed __int32)(Imm) < 128) { \
	    Emit8(0x83); EmitModRmReg(3,Reg,7); Emit8((unsigned __int8)(Imm)); \
	} else if (Reg == EAX_Reg) { \
		Emit8(0x3d); Emit32(Imm); \
	} else { \
		Emit8(0x81); EmitModRmReg(3,Reg,7); Emit32(Imm); \
	} \
}

#define Emit_CMP_Reg_DWORDPTR(Reg, p) { Emit8(0x3b); EmitModRmReg(0,5,Reg); EmitPtr(p); }

#define Emit_CMP_Reg_Reg(Reg1, Reg2)  { Emit8(0x3b); EmitModRmReg(3,Reg2,Reg1); }

#define Emit_TEST_Reg_Reg(Reg1, Reg2) { Emit8(0x85); EmitModRmReg(3,Reg1,Reg2); }

#define Emit_TEST_Reg_Imm32(Reg, Imm) { \
	if (Reg == EAX_Reg) { \
		Emit8(0xa9); Emit32(Imm); \
	} else { \
		Emit8(0xf7); EmitModRmReg(3,Reg,0); Emit32(Imm); \
	} \
}

#define Emit_INC_Reg(Reg) { Emit8(Reg+0x40); } // INC Reg
#define Emit_DEC_Reg(Reg) { Emit8(Reg+0x48); } // DEC Reg

#define Emit_OR_BYTEPTR_Imm8(p,Imm) { Emit8(0x80); EmitModRmReg(0,5,1); EmitPtr(p); Emit8(Imm); }  // OR B[p],Imm

#define Emit_SHL_Reg32_Imm(Reg,Imm) { Emit8(0xc1); EmitModRmReg(3,Reg,4); Emit8(Imm); }   // SHL Reg,Imm
#define Emit_SHR_Reg32_Imm(Reg,Imm) { Emit8(0xc1); EmitModRmReg(3,Reg,5); Emit8(Imm); }   // SHR Reg,Imm

// CWDE
#define Emit_CWDE() Emit8(0x98)

#define Emit_IMUL_Reg32(Reg) { Emit8(0xf7); EmitModRmReg(3,Reg,5); } // IMUL Reg

	 
#define Emit_MOVQ_MMReg_QWORDAtReg(MMReg,Reg) { Emit16(0x6f0f); EmitModRmReg(0,Reg,MMReg); }       // MOV MMReg,[Reg]  -  EAX, EBX, ECX or EDX only
#define Emit_MOVQ_QWORDAtReg_MMReg(Reg,MMReg) { Emit16(0x7f0f); EmitModRmReg(0,Reg,MMReg); }       // MOV [Reg],MMReg  -  EAX, EBX, ECX or EDX only
#define Emit_MOVQ_MMReg_QWORDPTR(MMReg,p) { Emit16(0x6f0f); EmitModRmReg(0,5,MMReg); EmitPtr(p); } // MOV MMReg,[p]
#define Emit_MOVQ_QWORDPTR_MMReg(p,MMReg) { Emit16(0x7f0f); EmitModRmReg(0,5,MMReg); EmitPtr(p); } // MOV [p],MMReg
// EMMS (reset registers so floating point can be used)
#define Emit_EMMS() Emit16(0x770f)

#define Emit_BT_DWORDPTR_Imm(p,Imm) {Emit16(0xba0f); EmitModRmReg(0,5,4); EmitPtr(p); Emit8(Imm); } // BT DWORDPTR, Imm8

#endif // __PLACE_H
