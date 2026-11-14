/* SPDX-License-Identifier: GPL-2.0-only */
#include "bgi/compat.h"
#include "bgi/version.h"
#include "bgi/vm.h"
#include "bgi/os.h"
#include <stdio.h>

static int failures;
#define CHECK(c) do { if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);failures++;} } while(0)
static void Put32(uint8_t* p,uint32_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24); }
static int Check(const uint8_t* code,uint32_t size,CompatResult_t* result)
{
	uint8_t file[128]={0};
	Put32(file,16);Put32(file+4,size);memcpy(file+16,code,size);
	return Compat_CheckProgram(file,16+size,result);
}
int main(void)
{
	CompatResult_t result;
	VmHandler_t keep;
	const uint8_t vector[]={0x5d,0x17}, immediate[]={0x02,0x5d,0,0,0,0x17};
	const uint8_t data[]={0x05,4,0,0x17,'a',0x5d,'b',0,0};
	const uint8_t missing[]={0xd0,0x40,0xd0,0x40,0x17};
	const uint8_t expanded[]={0x44,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5e,0x5f,0x64,0x65,0x91,0xbb,0x17};
	const uint8_t farJump[]={0x06,0xff,0x7f,0x16,0x17}, negativeData[]={0x05,0xff,0xff,0x17};
	uint8_t bad[16]={16};
	uint8_t shortened[18]={16};
	const char json[]="{\"games\":[{\"match\":\"CompatibilityFixture\",\"profile\":\"1.636\"}]}";
	const uint8_t boot[]="CompatibilityFixture";
	const uint8_t services[]={0x77,0x80,0x26,0x81,0x2c,0x81,0x2d,0x81,0x39,0x81,0xe0,0x17};
	OS_Init();
	CHECK(!Compat_Configure(NULL,"auto",NULL,0));
	CHECK(!Compat_Configure(NULL,"not-a-profile",NULL,0));
	CHECK(Compat_Configure(NULL,"1.636",NULL,0)!=NULL);
	CHECK(Check(vector,sizeof vector,&result) && result.count==0 && result.instructions==2);
	CHECK(Check(expanded,sizeof expanded,&result) && result.total==0 && result.problems==0 && result.instructions==13);
	Put32(shortened+4,32);shortened[16]=0x5d;shortened[17]=0x17;
	CHECK(Compat_CheckProgram(shortened,sizeof shortened,&result) && result.total==0 && result.instructions==2 && result.problems==1);
	keep=vm_optable_main[0x5d];vm_optable_main[0x5d]=Opcode_NotImplemented;
	CHECK(Check(vector,sizeof vector,&result) && result.count==1 && result.total==1);
	CHECK(result.issues[0].opcode==0x5d && result.issues[0].offset==0 && result.issues[0].kind==1);
	CHECK(Check(immediate,sizeof immediate,&result) && result.count==0 && result.instructions==2);
	CHECK(Check(data,sizeof data,&result) && result.count==0 && result.instructions==2);
	CHECK(Check(farJump,sizeof farJump,&result) && result.problems>0);
	CHECK(Check(negativeData,sizeof negativeData,&result) && result.problems>0);
	vm_optable_main[0x5d]=keep;
	CHECK(Check(missing,sizeof missing,&result) && result.total==1 && result.count==1);
	CHECK(result.issues[0].opcode==0xd040 && result.issues[0].kind==1 && result.issues[0].offset==0);
	CHECK(Compat_Configure(NULL,"1.616",NULL,0)!=NULL);
	CHECK(Check(services,sizeof services,&result) && result.total==0 && result.instructions==7);
	CHECK(Check(missing,sizeof missing,&result) && result.count==1 && result.issues[0].kind==2);
	CHECK(Check(vector,sizeof vector,&result) && result.problems>0); /* cannot decode an older profile's undefined main opcode */
	CHECK(!Compat_CheckProgram(NULL,0,&result));
	CHECK(!Compat_CheckProgram(bad,sizeof bad,&result));
	Put32(bad+4,0xfffffff0u);CHECK(!Compat_CheckProgram(bad,sizeof bad,&result));
	CHECK(OS_WriteNativeFile("bin/test_compat_games.json",json,sizeof json-1));
	CHECK(Compat_Configure("bin/test_compat_games.json","auto",boot,sizeof boot)!=NULL);
	CHECK(!strcmp(gEngine->name,"1.636"));
	CHECK(!Compat_Configure(NULL,"auto",boot,sizeof boot)); /* no leaked game table */
	remove("bin/test_compat_games.json");
	OS_Shutdown();
	if(failures)return 1;
	puts("Compatibility analyzer checks passed");return 0;
}
