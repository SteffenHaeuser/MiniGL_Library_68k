/*
** This file contains the runtime usable DLL functions, like LoadLibrary, GetProcAddress etc.
*/

#define __DLL_LIB_BUILD

#ifdef WARPUP
#pragma pack(push,2)
#endif
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "dll.h"
#ifdef WARPUP
#pragma pack(pop)
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void dllInternalFreeLibrary(int);

#define DLLOPENDLLS_MAX 20

struct dllOpenedDLL
{
	struct dll_sInstance *inst;
	int     usecount;
	char    name[100];
};

struct  dllOpenedDLL dllOpenedDLLs[DLLOPENDLLS_MAX];    //Maybe better use a linked list, but should work for now

void dllCleanup()
{
	int i;
	for(i=0;i<DLLOPENDLLS_MAX;i++)
		if(dllOpenedDLLs[i].inst)
			dllInternalFreeLibrary(i);
}

void *dllLoadLibrary(char *filename,char *portname)
{
	int (*Entry)(void *, long, void *);
	void *hinst = dllInternalLoadLibrary(filename,portname,1L);

	if (!hinst) return NULL;

	// Check for an entry point
	Entry = dllGetProcAddress(hinst, "DllEntryPoint");
	if (Entry)
	{
		int ret = Entry(hinst, 0, NULL);
		if (ret)
		{
			// if we get non-null here, assume the initialisation worked
			return hinst;
		}
		else
		{
			// the entry point reported an error
			dllFreeLibrary(hinst);
			return NULL;
		}
	}
	return hinst;
}

void *dllInternalLoadLibrary(char *filename,char *portname,int raiseusecount)
{
	struct dll_sInstance *inst;
	struct MsgPort *dllport;
	struct MsgPort *myport;
	dll_tMessage msg,*reply;
	static int cleanupflag=0;
	BPTR handle;
	int i;
	
	if(!cleanupflag)
	{
			bzero(&dllOpenedDLLs,sizeof(dllOpenedDLLs));
			
		if(atexit((void *)dllCleanup))
			return 0L;
		else
			cleanupflag=1L;
	}

	if(!filename)
		return 0L;  //Paranoia

	if(!(handle=Open(filename,MODE_OLDFILE)))
		return 0L;
	Close(handle);
	if(!portname)
		portname=filename;


// Search for already opened DLLs
	for(i=0;i<DLLOPENDLLS_MAX;i++)
	{
	if(dllOpenedDLLs[i].inst)
		{
			if(strcmp(dllOpenedDLLs[i].name,portname)==0)
			{
				if(raiseusecount)
				dllOpenedDLLs[i].usecount++;
				return dllOpenedDLLs[i].inst;
			}
		}
	}

// Not opened yet search a free slot

	for(i=0;i<DLLOPENDLLS_MAX;i++)
	if(!dllOpenedDLLs[i].inst)
		break;

	if(i==DLLOPENDLLS_MAX)
		return 0L;  // No free slot available

	if(!(inst=malloc(sizeof(struct dll_sInstance))))
		return 0L;
	if(!(myport=CreateMsgPort()))
	{
		free(inst);
		return 0L;
	}
	if(!(dllport=FindPort(portname)))
	{
		BPTR output = Open("CON:0/0/800/600/DLL_OUTPUT/AUTO/CLOSE/WAIT", MODE_NEWFILE);
		char commandline[256];
		int i;

		strcpy(commandline,filename);
		strcat(commandline," ");
		strcat(commandline,portname);

		SystemTags(commandline,
			SYS_Asynch, TRUE,
			SYS_Output, output,
			SYS_Input,  0L, //FIXME: some dll's might need stdin (0L: pointer NULL in a ULONG tag array is an error under gcc 15)
			NP_StackSize, 10000, //Messagehandler doesn't need a big stack (FIXME: but DLL_(De)Init might)
			TAG_DONE);

		for (i=0; i<20; i++)
		{
			dllport = FindPort(portname);
			if (dllport) break;
			//printf("Delaying...\n");
			Delay(25L);
		}
	}

	if(!dllport)
	{
		DeleteMsgPort(myport);
		free(inst);
		return 0L;
	}
	inst->dllPort=dllport;
	inst->StackType=DLLSTACK_DEFAULT;

	bzero(&msg, sizeof(msg));

	msg.dllMessageType=DLLMTYPE_Open;
	msg.dllMessageData.dllOpen.StackType = inst->StackType;
	msg.Message.mn_ReplyPort = myport;
	PutMsg(dllport, (struct Message *)&msg);
	WaitPort(myport);
	reply=(dll_tMessage *)GetMsg(myport);
	if (reply)
	{
		if(reply->dllMessageData.dllOpen.ErrorCode!=DLLERR_NoError)
		{
			DeleteMsgPort(myport);
			free(inst);
			return 0L;
		}
		
		//Obligatory symbol exports
		inst->FindResource = dllGetProcAddress(inst,"dllFindResource");
		inst->LoadResource = dllGetProcAddress(inst,"dllLoadResource");
		inst->FreeResource = dllGetProcAddress(inst,"dllFreeResource");

		if((inst->FindResource==0L)||
		   (inst->LoadResource==0L)||
		   (inst->FreeResource==0L))
		{
			DeleteMsgPort(myport);
			dllOpenedDLLs[i].inst=inst;
			dllInternalFreeLibrary(i);
			return 0L;
		}
	}
	else
	{
		//FIXME: Must/Can I send a Close message here ??
		DeleteMsgPort(myport);
		free(inst);
		return 0L;
	}
	DeleteMsgPort(myport);

	dllOpenedDLLs[i].inst=inst;
	dllOpenedDLLs[i].usecount=1;
	strcpy(dllOpenedDLLs[i].name,portname);
	
	return inst;
}

void dllFreeLibrary(void *hinst)
{
	int i;

	for(i=0;i<DLLOPENDLLS_MAX;i++)
		if(dllOpenedDLLs[i].inst==hinst)
			break;

	if(i==DLLOPENDLLS_MAX)
		return;         // ?????

	dllOpenedDLLs[i].usecount--;

	if(dllOpenedDLLs[i].usecount<=0)
		dllInternalFreeLibrary(i);
}

void dllInternalFreeLibrary(int i)
{
	dll_tMessage msg,*reply;
	struct MsgPort *myport;
	struct dll_sInstance *inst=(struct dll_sInstance *) dllOpenedDLLs[i].inst;
	
	if(!inst)
		return;

	if(!(myport=CreateMsgPort()))
	{
		exit(0L);       //Arghh
	}

	bzero(&msg, sizeof(msg));

	msg.dllMessageType=DLLMTYPE_Close;

	msg.Message.mn_ReplyPort = myport;

	if(FindPort(dllOpenedDLLs[i].name)==inst->dllPort)
	{
		PutMsg(inst->dllPort, (struct Message *)&msg);
		/*WaitPort(myport);*/
		while(!(reply=(dll_tMessage *)GetMsg(myport)))
		{
			Delay(2);
			if(FindPort(dllOpenedDLLs[i].name)!=inst->dllPort)
				break;
		}
	}

	DeleteMsgPort(myport);
	free(inst);

	bzero(&dllOpenedDLLs[i],sizeof(dllOpenedDLLs[i]));
	return;
}

	void *sym;
	

void *dllGetProcAddress(void *hinst,char *name)
{
	dll_tMessage msg,*reply;
	struct MsgPort *myport;
	struct dll_sInstance *inst=(struct dll_sInstance *) hinst;

	if(!hinst)
		return 0L;

	if(!(myport=CreateMsgPort()))
	{
		return 0L;
	}

	bzero(&msg, sizeof(msg));
	
	msg.dllMessageType=DLLMTYPE_SymbolQuery;
	msg.dllMessageData.dllSymbolQuery.StackType=inst->StackType;
	msg.dllMessageData.dllSymbolQuery.SymbolName=name;
	msg.dllMessageData.dllSymbolQuery.SymbolPointer=&sym;

	msg.Message.mn_ReplyPort = myport;
	PutMsg(inst->dllPort, (struct Message *)&msg);
	WaitPort(myport);
	reply=(dll_tMessage *)GetMsg(myport);

	DeleteMsgPort(myport);
	
	if(reply)
		return(sym);

	return 0L;
}

int dllKillLibrary(char *portname)
{
	dll_tMessage msg,*reply;
	struct MsgPort *myport;
	struct MsgPort *dllport;

	if(!(myport=CreateMsgPort()))
		exit(0L);       //Arghh
	
	bzero(&msg, sizeof(msg));

	msg.dllMessageType=DLLMTYPE_Kill;

	msg.Message.mn_ReplyPort = myport;

	if((dllport=FindPort(portname)))
	{
		PutMsg(dllport, (struct Message *)&msg);
		/*WaitPort(myport);*/
		while(!(reply=(dll_tMessage *)GetMsg(myport)))
		{
			Delay(2);
			if(FindPort(portname)!=dllport)
				break;
		}
	}

	DeleteMsgPort(myport);

	return (dllport?1:0);
}
