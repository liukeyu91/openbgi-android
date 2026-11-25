/* SPDX-License-Identifier: GPL-2.0-only */
#include <jni.h>
#include "bgi/compat.h"
#include "bgi/version.h"
#include "bgi/os_posix.h"

JNIEXPORT jobjectArray JNICALL Java_org_openbgi_port_NativeProgramInspector_identify(
	JNIEnv* env,jclass type,jstring path,jbyteArray boot)
{
	const char* config=path?(*env)->GetStringUTFChars(env,path,NULL):NULL;
	jobjectArray result=NULL;
	(void)type;
	if(path && !config)return NULL;
	if(boot){
		jsize size=(*env)->GetArrayLength(env,boot);
		jbyte* data=size<=COMPAT_MAX_PROGRAM?(*env)->GetByteArrayElements(env,boot,NULL):NULL;
		if(data){
			const char* selected=Compat_Configure(config,"auto",(const uint8_t*)data,(uint32_t)size);
			(*env)->ReleaseByteArrayElements(env,boot,data,JNI_ABORT);
			if(selected){
				jclass stringClass=(*env)->FindClass(env,"java/lang/String");
				if(stringClass)result=(*env)->NewObjectArray(env,2,stringClass,NULL);
				if(result){
					jstring profile=(*env)->NewStringUTF(env,selected);
					jstring product=profile?(*env)->NewStringUTF(env,Engine_ProductName()):NULL;
					if(product){
						(*env)->SetObjectArrayElement(env,result,0,product);
						(*env)->SetObjectArrayElement(env,result,1,profile);
					}
				}
			}
		}
	}
	if(config)(*env)->ReleaseStringUTFChars(env,path,config);
	return result;
}

JNIEXPORT jintArray JNICALL Java_org_openbgi_port_NativeProgramInspector_missingGlyphs(
	JNIEnv* env,jclass type,jstring family,jintArray codePoints)
{
	jint values[2048];
	uint32_t points[2048];
	(void)type;
	jsize count=(*env)->GetArrayLength(env,codePoints);
	if(count>2048)return NULL;
	(*env)->GetIntArrayRegion(env,codePoints,0,count,values);
	if((*env)->ExceptionCheck(env))return NULL;
	const char* name=(*env)->GetStringUTFChars(env,family,NULL);
	if(!name)return NULL;
	for(int i=0;i<count;i++)points[i]=(uint32_t)values[i];
	int missing=OsPosix_MissingGlyphs(name,points,count);
	(*env)->ReleaseStringUTFChars(env,family,name);
	if(missing<0)return NULL;
	for(int i=0;i<missing;i++)values[i]=(jint)points[i];
	jintArray output=(*env)->NewIntArray(env,missing);
	if(output)(*env)->SetIntArrayRegion(env,output,0,missing,values);
	return output;
}

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
