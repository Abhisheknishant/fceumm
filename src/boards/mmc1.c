/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 1998 BERO
 *  Copyright (C) 2002 Xodnizel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "mapinc.h"

static void GenMMC1Power(void);
static void GenMMC1Init(CartInfo *info, int prg, int chr, int wram, int battery);

static uint8 DRegs[4];
static uint8 Buffer, BufferShift;

static int mmc1opts;

static void (*MMC1CHRHook4)(uint32 A, uint8 V);
static void (*MMC1PRGHook16)(uint32 A, uint8 V);

static uint8 *WRAM = NULL;
static uint8 *CHRRAM = NULL;
static int is155, is171;

static uint8 MMC1WRAMEnabled(void) {
	return !(DRegs[3] & 0x10);
}

static DECLFW(MBWRAM) {
	if (MMC1WRAMEnabled() || is155)
		Page[A >> 11][A] = V;	/* WRAM is enabled. */
}

static DECLFR(MAWRAM) {
	if (!MMC1WRAMEnabled() && !is155)
		return X.DB;			/* WRAM is disabled */
	return(Page[A >> 11][A]);
}

static void MMC1CHR(void) {
	if (mmc1opts & 4) {
		if (DRegs[0] & 0x10)
			setprg8r(0x10, 0x6000, (DRegs[1] >> 4) & 1);
		else
			setprg8r(0x10, 0x6000, (DRegs[1] >> 3) & 1);
	}

	if (MMC1CHRHook4) {
		if (DRegs[0] & 0x10) {
			MMC1CHRHook4(0x0000, DRegs[1]);
			MMC1CHRHook4(0x1000, DRegs[2]);
		} else {
			MMC1CHRHook4(0x0000, (DRegs[1] & 0xFE));
			MMC1CHRHook4(0x1000, DRegs[1] | 1);
		}
	} else {
		if (DRegs[0] & 0x10) {
			setchr4(0x0000, DRegs[1]);
			setchr4(0x1000, DRegs[2]);
		} else
			setchr8(DRegs[1] >> 1);
	}
}

static void MMC1PRG(void) {
	uint8 offs = DRegs[1] & 0x10;
	if (MMC1PRGHook16) {
		switch (DRegs[0] & 0xC) {
		case 0xC:
			MMC1PRGHook16(0x8000, (DRegs[3] + offs));
			MMC1PRGHook16(0xC000, 0xF + offs);
			break;
		case 0x8:
			MMC1PRGHook16(0xC000, (DRegs[3] + offs));
			MMC1PRGHook16(0x8000, offs);
			break;
		case 0x0:
		case 0x4:
			MMC1PRGHook16(0x8000, ((DRegs[3] & ~1) + offs));
			MMC1PRGHook16(0xc000, ((DRegs[3] & ~1) + offs + 1));
			break;
		}
	} else {
		switch (DRegs[0] & 0xC) {
		case 0xC:
			setprg16(0x8000, (DRegs[3] + offs));
			setprg16(0xC000, 0xF + offs);
			break;
		case 0x8:
			setprg16(0xC000, (DRegs[3] + offs));
			setprg16(0x8000, offs);
			break;
		case 0x0:
		case 0x4:
			setprg16(0x8000, ((DRegs[3] & ~1) + offs));
			setprg16(0xc000, ((DRegs[3] & ~1) + offs + 1));
			break;
		}
	}
}

static void MMC1MIRROR(void) {
	if (!is171)
		switch (DRegs[0] & 3) {
		case 2: setmirror(MI_V); break;
		case 3: setmirror(MI_H); break;
		case 0: setmirror(MI_0); break;
		case 1: setmirror(MI_1); break;
		}
}

static uint64 lreset;
static DECLFW(MMC1_write) {
	int n = (A >> 13) - 4;

	/* The MMC1 is busy so ignore the write. */
	/* As of version FCE Ultra 0.81, the timestamp is only
		increased before each instruction is executed(in other words
		precision isn't that great), but this should still work to
		deal with 2 writes in a row from a single RMW instruction.
	*/
	if ((timestampbase + timestamp) < (lreset + 2))
		return;
/*	FCEU_printf("Write %04x:%02x\n",A,V); */
	if (V & 0x80) {
		DRegs[0] |= 0xC;
		BufferShift = Buffer = 0;
		MMC1PRG();
		lreset = timestampbase + timestamp;
		return;
	}

	Buffer |= (V & 1) << (BufferShift++);

	if (BufferShift == 5) {
/*		FCEU_printf("REG[%d]=%02x\n",n,Buffer); */
		DRegs[n] = Buffer;
		BufferShift = Buffer = 0;
		switch (n) {
		case 0: MMC1MIRROR(); MMC1CHR(); MMC1PRG(); break;
		case 1: MMC1CHR(); MMC1PRG(); break;
		case 2: MMC1CHR(); break;
		case 3: MMC1PRG(); break;
		}
	}
}

static void MMC1_Restore(int version) {
	MMC1MIRROR();
	MMC1CHR();
	MMC1PRG();
	lreset = 0;			/* timestamp(base) is not stored in save states. */
}

static void MMC1CMReset(void) {
	int i;

	for (i = 0; i < 4; i++)
		DRegs[i] = 0;
	Buffer = BufferShift = 0;
	DRegs[0] = 0x1F;

	DRegs[1] = 0;
	DRegs[2] = 0;		/* Should this be something other than 0? */
	DRegs[3] = 0;

	MMC1MIRROR();
	MMC1CHR();
	MMC1PRG();
}

static int DetectMMC1WRAMSize(uint32 crc32) {
	switch (crc32) {
	case 0xc6182024:			/* Romance of the 3 Kingdoms */
	case 0x2225c20f:			/* Genghis Khan */
	case 0x4642dda6:			/* Nobunaga's Ambition */
	case 0x29449ba9:			/* ""        "" (J) */
	case 0x2b11e0b0:			/* ""        "" (J) */
	case 0xb8747abf:			/* Best Play Pro Yakyuu Special (J) */
	case 0xc9556b36:			/* Final Fantasy I & II (J) [!] */
		FCEU_printf(" >8KB external WRAM present.  Use UNIF if you hack the ROM image.\n");
		return(16);
		break;
	case 0xd1e50064:            /* Dezaemon */
		FCEU_printf(" >8KB external WRAM present.  Use UNIF if you hack the ROM image.\n");
		return(32);
		break;
	default: return(8);
	}
}

static uint32 NWCIRQCount;
static uint8 NWCRec;
static int32 nwcdip = 0x4;

static void NWCIRQHook(int a) {
	if (!(NWCRec & 0x10)) {
		NWCIRQCount += a;
		if (NWCIRQCount >= (0x20000000 | (nwcdip << 25))) {
			NWCIRQCount = 0;
			X6502_IRQBegin(FCEU_IQEXT);
		}
	}
}

static void NWCCHRHook(uint32 A, uint8 V) {
	if ((V & 0x10)) {	/* && !(NWCRec&0x10)) */
		NWCIRQCount = 0;
		X6502_IRQEnd(FCEU_IQEXT);
	}

	NWCRec = V;
	if (V & 0x08)
		MMC1PRG();
	else
		setprg32(0x8000, (V >> 1) & 3);
}

static void NWCPRGHook(uint32 A, uint8 V) {
	if (NWCRec & 0x8)
		setprg16(A, 8 | (V & 0x7));
	else
		setprg32(0x8000, (NWCRec >> 1) & 3);
}

static void NWCPower(void) {
	GenMMC1Power();
	setchr8r(0, 0);
	nwcdip = (int32)GameInfo->cspecial;
}

static void NWCReset(void) {
	nwcdip = (int32)GameInfo->cspecial;
}

void Mapper105_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 256, 8, 0);
	MMC1CHRHook4 = NWCCHRHook;
	MMC1PRGHook16 = NWCPRGHook;
	MapIRQHook = NWCIRQHook;
	info->Power = NWCPower;
	info->Reset = NWCReset;
}

static void GenMMC1Power(void) {
	lreset = 0;
	if (mmc1opts & 1) {
		FCEU_CheatAddRAM(8, 0x6000, WRAM);
		if (mmc1opts & 4)
			FCEU_dwmemset(WRAM, 0, 8192)
			else if (!(mmc1opts & 2))
				FCEU_dwmemset(WRAM, 0, 8192);
	}
	SetWriteHandler(0x8000, 0xFFFF, MMC1_write);
	SetReadHandler(0x8000, 0xFFFF, CartBR);

	if (mmc1opts & 1) {
		SetReadHandler(0x6000, 0x7FFF, MAWRAM);
		SetWriteHandler(0x6000, 0x7FFF, MBWRAM);
		setprg8r(0x10, 0x6000, 0);
	}

	MMC1CMReset();
}

static void GenMMC1Close(void) {
	if (CHRRAM)
		FCEU_gfree(CHRRAM);
	if (WRAM)
		FCEU_gfree(WRAM);
	CHRRAM = WRAM = NULL;
}

static void GenMMC1Init(CartInfo *info, int prg, int chr, int wram, int battery) {
	is155 = 0;

	info->Close = GenMMC1Close;
	MMC1PRGHook16 = MMC1CHRHook4 = 0;
	mmc1opts = 0;
	PRGmask16[0] &= (prg >> 14) - 1;
	CHRmask4[0] &= (chr >> 12) - 1;
	CHRmask8[0] &= (chr >> 13) - 1;

	if (wram) {
		WRAM = (uint8*)FCEU_gmalloc(wram * 1024);
		mmc1opts |= 1;
		if (wram > 8) mmc1opts |= 4;
		SetupCartPRGMapping(0x10, WRAM, wram * 1024, 1);
		AddExState(WRAM, wram * 1024, 0, "WRAM");
		if (battery) {
			mmc1opts |= 2;
			info->SaveGame[0] = WRAM + ((mmc1opts & 4) ? 8192 : 0);
			info->SaveGameLen[0] = 8192;
		}
	}
	if (!chr) {
		CHRRAM = (uint8*)FCEU_gmalloc(8192);
		SetupCartCHRMapping(0, CHRRAM, 8192, 1);
		AddExState(CHRRAM, 8192, 0, "CHRR");
	}
	AddExState(DRegs, 4, 0, "DREG");

	info->Power = GenMMC1Power;
	GameStateRestore = MMC1_Restore;
	AddExState(&lreset, 8, 1, "LRST");
	AddExState(&Buffer, 1, 1, "BFFR");
	AddExState(&BufferShift, 1, 1, "BFRS");
}

void Mapper1_Init(CartInfo *info) {
	int ws = DetectMMC1WRAMSize(info->CRC32);
	GenMMC1Init(info, 512, 256, ws, info->battery);
}

/* Same as mapper 1, without respect for WRAM enable bit. */
void Mapper155_Init(CartInfo *info) {
	GenMMC1Init(info, 512, 256, 8, info->battery);
	is155 = 1;
}

/* Same as mapper 1, with different (or without) mirroring control. */
/* Kaiser KS7058 board, KS203 custom chip */
void Mapper171_Init(CartInfo *info) {
	GenMMC1Init(info, 32, 32, 0, 0);
	is171 = 1;
}

void SAROM_Init(CartInfo *info) {
	GenMMC1Init(info, 128, 64, 8, info->battery);
}

void SBROM_Init(CartInfo *info) {
	GenMMC1Init(info, 128, 64, 0, 0);
}

void SCROM_Init(CartInfo *info) {
	GenMMC1Init(info, 128, 128, 0, 0);
}

void SEROM_Init(CartInfo *info) {
	GenMMC1Init(info, 32, 64, 0, 0);
}

void SGROM_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 0, 0, 0);
}

void SKROM_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 64, 8, info->battery);
}

void SLROM_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 128, 0, 0);
}

void SL1ROM_Init(CartInfo *info) {
	GenMMC1Init(info, 128, 128, 0, 0);
}

/* Begin unknown - may be wrong - perhaps they use different MMC1s from the
	similarly functioning boards?
*/

void SL2ROM_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 256, 0, 0);
}

void SFROM_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 256, 0, 0);
}

void SHROM_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 256, 0, 0);
}

/* End unknown  */
/*              */
/*              */

void SNROM_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 0, 8, info->battery);
}

void SOROM_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 0, 16, info->battery);
}

/* ----------------------- FARID_SLROM_8-IN-1 -----------------------*/

/* NES 2.0 Mapper 323 - UNIF FARID_SLROM_8-IN-1 */

static uint8 reg, lock;

static void FARIDSLROM8IN1PRGHook(uint32 A, uint8 V) {
	setprg16(A, (V & 0x07) | (reg << 3));
}

static void FARIDSLROM8IN1CHRHook(uint32 A, uint8 V) {
	setchr4(A, (V & 0x1F) | (reg << 5));
}

static DECLFW(FARIDSLROM8IN1Write) {
	if (MMC1WRAMEnabled() && !lock) {
		lock = (V & 0x08) >> 3;
		reg = (V & 0xF0) >> 4;
		MMC1MIRROR();
		MMC1CHR();
		MMC1PRG();
	}
}

static void FARIDSLROM8IN1Power(void) {
	reg = lock = 0;
	GenMMC1Power();
	SetWriteHandler(0x6000, 0x7FFF, FARIDSLROM8IN1Write);
}

static void FARIDSLROM8IN1Reset(void) {
	reg = lock = 0;
	MMC1CMReset();
}

void FARIDSLROM8IN1_Init(CartInfo *info) {
	GenMMC1Init(info, 1024, 256, 8, 0);
	MMC1CHRHook4 = FARIDSLROM8IN1CHRHook;
	MMC1PRGHook16 = FARIDSLROM8IN1PRGHook;
	info->Power = FARIDSLROM8IN1Power;
	info->Reset = FARIDSLROM8IN1Reset;
	AddExState(&lock, 1, 0, "LOCK");
	AddExState(&reg, 1, 0, "REG6");
}

/* ---------------------------- Mapper 374 -------------------------------- */
/* 1995 Super HiK 4-in-1 - 新系列機器戰警组合卡 (JY-022)
 * 1996 Super HiK 4-in-1 - 新系列超級飛狼組合卡 (JY-051)
 */
static uint8 game = 0;
static void M374PRG(uint32 A, uint8 V) {
	setprg16(A, (V & 0x07) | (game << 3));
}

static void M374CHR(uint32 A, uint8 V) {
	setchr4(A, (V & 0x1F) | (game << 5));
}

static void M374Reset(void) {
	game = (game + 1) & 3;
	MMC1CMReset();
}

void Mapper374_Init(CartInfo *info) {
	GenMMC1Init(info, 128, 128, 0, 0);
	MMC1CHRHook4 = M374CHR;
	MMC1PRGHook16 = M374PRG;
	info->Reset = M374Reset;
	AddExState(&game, 1, 0, "GAME");
}

/* ---------------------------- Mapper 297 -------------------------------- */
/* NES 2.0 Mapper 297 - 2-in-1 Uzi Lightgun (MGC-002) */

static uint8 mode;
static uint8 latch;

static void M297PRG(uint32 A, uint8 V) {
	setprg16(A, (V & 0x07) | ((mode & 1) << 3));
}

static void M297CHR(uint32 A, uint8 V) {
	setchr4(A, (V & 0x1F) | ((mode & 1) << 5));
}

static void Sync(void) {
	if (mode & 1) {
		/* MMC1 */
        MMC1PRG();
		MMC1CHR();
		MMC1MIRROR();
    } else {		
		/* Mapper 70 */
        setprg16(0x8000, ((mode & 2) << 1) | ((latch >> 4) & 3));
        setprg16(0xC000, ((mode & 2) << 1) | 3);
        setchr8(latch & 0xF);
        setmirror(1);
    }
}

static DECLFW(M297Mode) {
    if (A & 0x100) {
        mode = V;
        Sync();
    }
}

static DECLFW(M297Latch) {
	if (mode & 1) {
		MMC1_write(A, V);
	} else {
		latch = V;
		Sync();
	}
}

static void M297Power(void) {
	latch = 0;
	mode = 0;
	Sync();
	GenMMC1Power();
	SetWriteHandler(0x4120, 0x4120, M297Mode);
	SetWriteHandler(0x8000, 0xFFFF, M297Latch);
}

void Mapper297_Init(CartInfo *info) {
	GenMMC1Init(info, 256, 256, 0, 0);
	info->Power = M297Power;
	MMC1CHRHook4 = M297CHR;
	MMC1PRGHook16 = M297PRG;
	AddExState(&latch, 1, 0, "LATC");
	AddExState(&mode, 1, 0, "MODE");
}
