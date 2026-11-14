/* SPDX-License-Identifier: GPL-2.0-only */
#include "bgi/compat.h"
#include "bgi/asm.h"
#include "bgi/version.h"
#include "bgi/vm.h"

const char* Compat_Configure(const char* gamesJson, const char* profile, const uint8_t* boot, uint32_t size)
{
	Engine_ResetGames();
	if(gamesJson && *gamesJson && !Engine_LoadGamesJson(gamesJson))
		return NULL;
	if(profile && strcmp(profile, "auto"))
	{
		if(!Engine_SelectProfile(profile)) return NULL;
	}
	else if(!boot || !size || !Engine_DetectGame(boot, size))
		return NULL;
	Vm_TablesInit();
	return gEngine->name;
}

static uint32_t Read32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int Compat_CheckProgram(const uint8_t* file, uint32_t size, CompatResult_t* result)
{
	static const struct { int family; VmHandler_t* table; } tables[] = {
		{0,vm_optable_main},{0x7f,vm_optable_7F},{0x80,vm_optable_80},{0x81,vm_optable_81},
		{0x90,vm_optable_90},{0x91,vm_optable_91},{0x92,vm_optable_92},{0xa0,vm_optable_A0},
		{0xb0,vm_optable_B0},{0xc0,vm_optable_C0},{0xd0,vm_optable_D0},{0xe0,vm_optable_E0},{0xef,vm_optable_EF}
	};
	uint8_t seen[BGI_COUNTOF(tables)][256] = {{0}};
	DisMap_t map;
	uint32_t off;
	memset(result,0,sizeof *result);
	/* Bound the declared extent too. Shortened translation files may retain
	 * their original size: analyze the available bytes and report the header
	 * discrepancy through Dis_Analyze instead of skipping the whole module. */
	if(!file || size < 16 || size > COMPAT_MAX_PROGRAM || Read32(file) != 16 ||
		size == 16 || !Read32(file+4) || Read32(file+4) > COMPAT_MAX_PROGRAM)
		return 0;
	if(!Dis_Analyze(file,size,gEngine->gen,NULL,&map))return 0;
	result->problems = (uint32_t)map.problems;
	for(off=0;off<map.size;)
	{
		AsmInsn_t in;
		size_t i;
		if(map.kind[off] != DIS_K_CODE) { ++off; continue; }
		if(!Asm_Decode(map.area,map.size,off,gEngine->gen,&in)) { ++result->problems; ++off; continue; }
		++result->instructions;
		for(i=0;i<BGI_COUNTOF(tables);++i)if(tables[i].family == in.fam)
		{
			VmHandler_t handler=tables[i].table[in.op];
			if((!handler || handler == Opcode_NotImplemented || in.foreign) && !seen[i][in.op])
			{
				seen[i][in.op]=1; ++result->total;
				if(result->count < COMPAT_MAX_ISSUES)
				{
					CompatIssue_t* issue=&result->issues[result->count++];
					issue->opcode=(uint32_t)(in.fam << 8) | (uint32_t)in.op;
					issue->offset=off;
					issue->kind=(!handler || in.foreign)?2:1;
				}
			}
			break;
		}
		off += in.len;
	}
	Dis_FreeMap(&map);
	return 1;
}
