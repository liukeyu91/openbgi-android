/* SPDX-License-Identifier: GPL-2.0-only
 *
 * disc_jni.c - the launcher's read-only view of a disc image. The library
 * keeps images out of the app's storage: the engine mounts the same image
 * when the game starts (storage.c), and this probe only reports what the
 * image holds so the library can validate an entry before it is added.
 */
#include <jni.h>
#include <inttypes.h>
#include <string.h>

#include "android_internal.h"
#include "../posix/disc_internal.h"

/* One line per entry: "relative/path<TAB>data<TAB>size<TAB>csize<TAB>method<TAB>flags".
 * The path is relative to the game root the mount chose, so the Java side
 * sees the same layout a directory game would have; data is the first byte
 * of the stored bytes and csize their count (the same as size for a disc
 * image, smaller for a deflated ZIP entry, which method 8 marks); flags is
 * 1 = directory, 2 = readable, 4 = stored contiguously. */
JNIEXPORT jobjectArray JNICALL Java_org_openbgi_port_DiscImage_nativeEntries(
	JNIEnv* env,jclass type,jstring image)
{
	const char* path;
	jobjectArray result=NULL;
	jclass stringClass;
	uint32_t count,index;
	size_t rootLength;
	const char* root;
	(void)type;
	if(!image)return NULL;
	path=(*env)->GetStringUTFChars(env,image,NULL);
	if(!path)return NULL;
	if(!Disc_Mounted() || strcmp(Disc_ImagePath(),path))
		if(!OsPosix_MountDisc(path))
		{
			(*env)->ReleaseStringUTFChars(env,image,path);
			return NULL;
		}
	(*env)->ReleaseStringUTFChars(env,image,path);
	stringClass=(*env)->FindClass(env,"java/lang/String");
	if(!stringClass)return NULL;
	count=Disc_Count();
	root=Disc_Root();
	rootLength=strlen(root);
	if(rootLength && root[rootLength-1]=='/')
		rootLength--;
	result=(*env)->NewObjectArray(env,(jsize)count,stringClass,NULL);
	if(!result)return NULL;
	for(index=0;index<count;++index)
	{
		const VolEntry_t* entry=Disc_At(index);
		char line[VOL_NAME_MAX+96];
		jstring value;
		if(!entry || entry->path==NULL)continue;
		{
			const char* relative=entry->path;
			int flags=(entry->isDir?1:0)|(entry->readable?2:0)|(entry->stored?4:0);
			/* The game root itself is the directory the paths are relative to. */
			if(rootLength && !strncmp(relative,root,rootLength))
			{
				if(!relative[rootLength])
					continue;
				if(relative[rootLength]=='/')
					relative+=rootLength+1;
			}
			if(relative[0]=='/')
				relative++;
			if(!relative[0])
				continue;
			if(snprintf(line,sizeof line,"%s\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%u\t%d",
				relative,entry->data,entry->size,entry->csize,(unsigned)entry->method,flags)>=(int)sizeof line)
				continue;
		}
		value=(*env)->NewStringUTF(env,line);
		if(!value)return result;
		(*env)->SetObjectArrayElement(env,result,(jsize)index,value);
		(*env)->DeleteLocalRef(env,value);
		if((*env)->ExceptionCheck(env))return result;
	}
	return result;
}

/* Release the index the probe mounted; the engine mounts its own copy in
 * the game process, so nothing here outlives the launcher's probe. */
JNIEXPORT void JNICALL Java_org_openbgi_port_DiscImage_nativeRelease(JNIEnv* env,jclass type)
{
	(void)env;
	(void)type;
	OsPosix_MountDisc(NULL);
}
