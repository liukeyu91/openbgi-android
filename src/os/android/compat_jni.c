/* SPDX-License-Identifier: GPL-2.0-only */
#include <jni.h>
#include "bgi/compat.h"

JNIEXPORT jstring JNICALL Java_org_openbgi_port_NativeProgramInspector_configure(
	JNIEnv* env,jclass type,jstring path,jstring profile,jbyteArray boot)
{
	const char* p=NULL;
	const char* pref=NULL;
	jbyte* data=NULL;
	jsize size=0;
	const char* selected;
	jstring result=NULL;
	(void)type;
	if(path){p=(*env)->GetStringUTFChars(env,path,NULL);if(!p)goto done;}
	if(profile){pref=(*env)->GetStringUTFChars(env,profile,NULL);if(!pref)goto done;}
	if(boot){
		size=(*env)->GetArrayLength(env,boot);if(size>(jsize)COMPAT_MAX_PROGRAM)goto done;
		data=(*env)->GetByteArrayElements(env,boot,NULL);if(!data)goto done;
	}
	if(!(*env)->ExceptionCheck(env)) {
		selected=Compat_Configure(p,pref,(const uint8_t*)data,(uint32_t)size);
		if(selected)result=(*env)->NewStringUTF(env,selected);
	}
done:
	if(data)(*env)->ReleaseByteArrayElements(env,boot,data,JNI_ABORT);
	if(pref)(*env)->ReleaseStringUTFChars(env,profile,pref);
	if(p)(*env)->ReleaseStringUTFChars(env,path,p);
	return result;
}

JNIEXPORT jintArray JNICALL Java_org_openbgi_port_NativeProgramInspector_inspect(
	JNIEnv* env,jclass type,jbyteArray program)
{
	CompatResult_t result;
	jint values[5+COMPAT_MAX_ISSUES*3]={0};
	jbyte* data;
	jsize size;
	jintArray output;
	int i,count=5;
	(void)type;
	if(!program)return NULL;
	size=(*env)->GetArrayLength(env,program);
	if(size > (jsize)COMPAT_MAX_PROGRAM)return NULL;
	data=(*env)->GetByteArrayElements(env,program,NULL);
	if(!data)return NULL;
	values[0]=Compat_CheckProgram((const uint8_t*)data,(uint32_t)size,&result);
	(*env)->ReleaseByteArrayElements(env,program,data,JNI_ABORT);
	if(values[0]) {
		values[1]=(jint)result.instructions;values[2]=(jint)result.problems;
		values[3]=(jint)result.total;values[4]=result.count;
		for(i=0;i<result.count;++i) {
			values[count++]=(jint)result.issues[i].opcode;
			values[count++]=(jint)result.issues[i].offset;
			values[count++]=result.issues[i].kind;
		}
	}
	output=(*env)->NewIntArray(env,count);
	if(output)(*env)->SetIntArrayRegion(env,output,0,count,values);
	return output;
}
