/*

pidvbip - tvheadend client for the Raspberry Pi

(C) Dave Chapman 2012-2013

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdio.h>
#include <string.h>

#include "omx_utils.h"

/* From omxtx */
/* Print some useful information about the state of the port: */
static void dumpport(OMX_HANDLETYPE handle, int port)
{
  OMX_VIDEO_PORTDEFINITIONTYPE  *viddef;
  OMX_PARAM_PORTDEFINITIONTYPE  portdef;

  OMX_INIT_STRUCTURE(portdef);
  portdef.nPortIndex = port;
  OERR(OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &portdef));

  printf("Port %d is %s, %s\n", portdef.nPortIndex,
    (portdef.eDir == 0 ? "input" : "output"),
    (portdef.bEnabled == 0 ? "disabled" : "enabled"));
  printf("Wants %d bufs, needs %d, size %d, enabled: %d, pop: %d, "
    "aligned %d\n", portdef.nBufferCountActual,
    portdef.nBufferCountMin, portdef.nBufferSize,
    portdef.bEnabled, portdef.bPopulated,
    portdef.nBufferAlignment);
  viddef = &portdef.format.video;

  switch (portdef.eDomain) {
  case OMX_PortDomainVideo:
    printf("Video type is currently:\n"
      "\tMIME:\t\t%s\n"
      "\tNative:\t\t%p\n"
      "\tWidth:\t\t%d\n"
      "\tHeight:\t\t%d\n"
      "\tStride:\t\t%d\n"
      "\tSliceHeight:\t%d\n"
      "\tBitrate:\t%d\n"
      "\tFramerate:\t%d (%x); (%f)\n"
      "\tError hiding:\t%d\n"
      "\tCodec:\t\t%d\n"
      "\tColour:\t\t%d\n",
      viddef->cMIMEType, viddef->pNativeRender,
      viddef->nFrameWidth, viddef->nFrameHeight,
      viddef->nStride, viddef->nSliceHeight,
      viddef->nBitrate,
      viddef->xFramerate, viddef->xFramerate,
      ((float)viddef->xFramerate/(float)65536),
      viddef->bFlagErrorConcealment,
      viddef->eCompressionFormat, viddef->eColorFormat);
    break;
  case OMX_PortDomainImage:
    printf("Image type is currently:\n"
      "\tMIME:\t\t%s\n"
      "\tNative:\t\t%p\n"
      "\tWidth:\t\t%d\n"
      "\tHeight:\t\t%d\n"
      "\tStride:\t\t%d\n"
      "\tSliceHeight:\t%d\n"
      "\tError hiding:\t%d\n"
      "\tCodec:\t\t%d\n"
      "\tColour:\t\t%d\n",
      portdef.format.image.cMIMEType,
      portdef.format.image.pNativeRender,
      portdef.format.image.nFrameWidth,
      portdef.format.image.nFrameHeight,
      portdef.format.image.nStride,
      portdef.format.image.nSliceHeight,
      portdef.format.image.bFlagErrorConcealment,
      portdef.format.image.eCompressionFormat,
      portdef.format.image.eColorFormat);     
    break;
/* Feel free to add others. */
  default:
    break;
  }
}

OMX_ERRORTYPE omx_send_command_and_wait0(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData)
{
   pthread_mutex_lock(&component->cmd_queue_mutex);
   component->cmd.hComponent = component->h;
   component->cmd.Cmd = Cmd;
   component->cmd.nParam = nParam;
   component->cmd.pCmdData = pCmdData;
   pthread_mutex_unlock(&component->cmd_queue_mutex);

   OMX_SendCommand(component->h, Cmd, nParam, pCmdData);
}

OMX_ERRORTYPE omx_send_command_and_wait1(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData)
{
   pthread_mutex_lock(&component->cmd_queue_mutex);
   while (component->cmd.hComponent) {
     /* pthread_cond_wait releases the mutex (which must be locked) and blocks on the condition variable */
     pthread_cond_wait(&component->cmd_queue_count_cv,&component->cmd_queue_mutex);
   }
   pthread_mutex_unlock(&component->cmd_queue_mutex);

   //fprintf(stderr,"Command completed\n");
}

OMX_ERRORTYPE omx_send_command_and_wait(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData)
{
  omx_send_command_and_wait0(component,Cmd,nParam,pCmdData);
  omx_send_command_and_wait1(component,Cmd,nParam,pCmdData);
}

//void omx_wait_for_event(struct omx_pipeleine_t* pipe, OMX_HANDLETYPE *hComponent
//			//&pipe,video_render, OMXEventBufferFlag, 90, OMX_BUFFERFLAG_EOS)
//{
//
//}

static char* mapcomponent(struct omx_pipeline_t* pipe, OMX_HANDLETYPE component)
{
  if (component == pipe->video_decode.h) return "video_decode";
  else if (component == pipe->video_scheduler.h) return "video_scheduler";
  else if (component == pipe->video_render.h) return "video_render";
  else if (component == pipe->clock.h) return "clock";

  return "<unknown>";
}

/* The event handler is called from the OMX component thread */
static OMX_ERRORTYPE omx_event_handler(OMX_IN OMX_HANDLETYPE hComponent,
                                       OMX_IN OMX_PTR pAppData,
                                       OMX_IN OMX_EVENTTYPE eEvent,
                                       OMX_IN OMX_U32 nData1,
                                       OMX_IN OMX_U32 nData2,
                                       OMX_IN OMX_PTR pEventData)
{
  struct omx_component_t* component = (struct omx_component_t*)pAppData;

//  fprintf(stderr,"[EVENT]: threadid=%u\n",(unsigned int)pthread_self());

  switch (eEvent) {
    case OMX_EventError:
      fprintf(stderr,"[EVENT] %s %p has errored: %x\n", mapcomponent(component->pipe, hComponent), hComponent, (unsigned int)nData1);
      exit(1);
//fprintf(stderr,"[EVENT] Waiting for lock\n");
      pthread_mutex_lock(&component->cmd_queue_mutex);
//fprintf(stderr,"[EVENT] Got lock - cmd.hComponent=%x\n",(unsigned int)pipe->cmd.hComponent);
      memset(&component->cmd,0,sizeof(component->cmd));
//fprintf(stderr,"[EVENT] Clearing cmd\n");
      pthread_cond_signal(&component->cmd_queue_count_cv);
      pthread_mutex_unlock(&component->cmd_queue_mutex);
//fprintf(stderr,"[EVENT] Returning from event\n");
      return nData1;
      break;

    case OMX_EventCmdComplete:
      //fprintf(stderr,"[EVENT] %s %p has completed the last command (%x).\n", mapcomponent(pipe, hComponent), hComponent, nData1);

//fprintf(stderr,"[EVENT] Waiting for lock\n");
      pthread_mutex_lock(&component->cmd_queue_mutex);
//fprintf(stderr,"[EVENT] Got lock\n");
      if ((nData1 == component->cmd.Cmd) &&
          (nData2 == component->cmd.nParam)) {
         memset(&component->cmd,0,sizeof(component->cmd));
         pthread_cond_signal(&component->cmd_queue_count_cv);
      }
      pthread_mutex_unlock(&component->cmd_queue_mutex);

      break;

    case OMX_EventPortSettingsChanged: 
      fprintf(stderr,"[EVENT] %s %p port %d settings changed.\n", mapcomponent(component->pipe, hComponent), hComponent, (unsigned int)nData1);
      component->port_settings_changed = 1;
      break;

  case OMX_EventBufferFlag:
    //fprintf(stderr,"[EVENT] Got an EOS event on %s %p (port %d, d2 %x)\n", mapcomponent(pipe, hComponent), hComponent, (unsigned int)nData1, (unsigned int)nData2);

    if (nData2 == OMX_BUFFERFLAG_EOS) {
      pthread_mutex_lock(&component->eos_mutex);
      component->eos = 1;
      pthread_cond_signal(&component->eos_cv);
      pthread_mutex_unlock(&component->eos_mutex);
    }

    break;

    default:
      fprintf(stderr,"[EVENT] Got an event of type %x on %s %p (d1: %x, d2 %x)\n", eEvent,
       mapcomponent(component->pipe, hComponent), hComponent, (unsigned int)nData1, (unsigned int)nData2);
  }
        
  return OMX_ErrorNone;
}

static OMX_ERRORTYPE omx_empty_buffer_done(OMX_IN OMX_HANDLETYPE hComponent,
                                           OMX_IN OMX_PTR pAppData,
                                           OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
  struct omx_component_t* component = (struct omx_component_t*)pAppData;

  if (component->buf_notempty == 0) {
    pthread_mutex_lock(&component->buf_mutex);
    component->buf_notempty = 1;
    pthread_cond_signal(&component->buf_notempty_cv);
    pthread_mutex_unlock(&component->buf_mutex);
  }
  return OMX_ErrorNone;
}

static OMX_ERRORTYPE omx_fill_buffer_done(OMX_IN OMX_HANDLETYPE hComponent,
                                          OMX_IN OMX_PTR pAppData,
                                          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
  fprintf(stderr,"[omx_fill_buffer_done]\n");
  return OMX_ErrorNone;
}

/* Function based on ilclient_create_component() */
static void omx_disable_all_ports(struct omx_component_t* component)
{
  OMX_PORT_PARAM_TYPE ports;
  OMX_INDEXTYPE types[] = {OMX_IndexParamAudioInit, OMX_IndexParamVideoInit, OMX_IndexParamImageInit, OMX_IndexParamOtherInit};
  int i;

  ports.nSize = sizeof(OMX_PORT_PARAM_TYPE);
  ports.nVersion.nVersion = OMX_VERSION;

  for(i=0; i<4; i++) {
    OMX_ERRORTYPE error = OMX_GetParameter(component->h, types[i], &ports);
    if(error == OMX_ErrorNone) {
      uint32_t j;
      for(j=0; j<ports.nPorts; j++) {
        //fprintf(stderr,"Disabling port %lu\n",ports.nStartPortNumber+j);
        omx_send_command_and_wait(component, OMX_CommandPortDisable, ports.nStartPortNumber+j, NULL);
      }
    }
  }
}

/* Based on allocbufs from omxtx.
   Buffers are connected as a one-way linked list using pAppPrivate as the pointer to the next element */
static OMX_BUFFERHEADERTYPE* omx_alloc_buffers(struct omx_pipeline_t *pipe, OMX_HANDLETYPE component, int port)
{
  int i;
  OMX_BUFFERHEADERTYPE *list = NULL, **end = &list;
  OMX_PARAM_PORTDEFINITIONTYPE portdef;

  OMX_INIT_STRUCTURE(portdef);
  portdef.nPortIndex = port;

  OERR(OMX_GetParameter(component, OMX_IndexParamPortDefinition, &portdef));

  //fprintf(stderr,"Allocating %d buffers of %d bytes\n",(int)portdef.nBufferCountActual,(int)portdef.nBufferSize);
  //fprintf(stderr,"portdef.bEnabled=%d\n",portdef.bEnabled);

  for (i = 0; i < portdef.nBufferCountActual; i++) {
    OMX_U8 *buf;

    buf = vcos_malloc_aligned(portdef.nBufferSize, portdef.nBufferAlignment, "buffer");

    //    printf("Allocated a buffer of %u bytes\n",(unsigned int)portdef.nBufferSize);

    OERR(OMX_UseBuffer(component, end, port, NULL, portdef.nBufferSize, buf));

    end = (OMX_BUFFERHEADERTYPE **) &((*end)->pAppPrivate);
  }

  return list;
}

void omx_free_buffers(struct omx_component_t *component, int port)
{
  OMX_BUFFERHEADERTYPE *buf, *prev;

  buf = component->buffers;
  while (buf) {
    prev = buf->pAppPrivate;
    OERR(OMX_FreeBuffer(component->h, port, buf)); /* This also calls free() */
    buf = prev;
  }
}

int omx_get_free_buffer_count(struct omx_component_t* component)
{
  int n = 0;
  OMX_BUFFERHEADERTYPE *buf = component->buffers;

  pthread_mutex_lock(&component->buf_mutex);
  while (buf) {
    if (buf->nFilledLen == 0) n++;
    buf = buf->pAppPrivate;
  }
  pthread_mutex_unlock(&component->buf_mutex);

  return n;
}

static void dump_buffer_status(OMX_BUFFERHEADERTYPE *buffers)
{
  OMX_BUFFERHEADERTYPE *buf = buffers;

  while (buf) {
    fprintf(stderr,"*****\n");
    fprintf(stderr,"buf->pAppPrivate=%u\n",(unsigned int)buf->pAppPrivate);
    fprintf(stderr,"buf->nAllocLen=%u\n",(unsigned int)buf->nAllocLen);
    fprintf(stderr,"buf->nFilledLen=%u\n",(unsigned int)buf->nFilledLen);
    fprintf(stderr,"buf->pBuffer=%u\n",(unsigned int)buf->pBuffer);
    fprintf(stderr,"buf->nOffset=%u\n",(unsigned int)buf->nOffset);
    fprintf(stderr,"buf->nFlags=%u\n",(unsigned int)buf->nFlags);
    fprintf(stderr,"buf->nInputPortIndex=%u\n",(unsigned int)buf->nInputPortIndex);
    fprintf(stderr,"buf->nOutputPortIndex=%u\n",(unsigned int)buf->nOutputPortIndex);
    buf = buf->pAppPrivate;
  }
}

void summarise_buffers(OMX_BUFFERHEADERTYPE *buffers)
{
  OMX_BUFFERHEADERTYPE *buf = buffers;

  fprintf(stderr,"*******\n");
  while (buf) {
    fprintf(stderr,"buf->nFilledLen=%u\n",(unsigned int)buf->nFilledLen);
    buf = buf->pAppPrivate;
  }
}

/* Return the next free buffer, or NULL if none are free */
OMX_BUFFERHEADERTYPE *get_next_buffer(struct omx_component_t* component)
{
  OMX_BUFFERHEADERTYPE *ret;

retry:
  pthread_mutex_lock(&component->buf_mutex);

  ret = component->buffers;
  while (ret && ret->nFilledLen > 0)
    ret = ret->pAppPrivate;

  if (!ret)
    component->buf_notempty = 0;

  if (ret) {
    pthread_mutex_unlock(&component->buf_mutex);
    return ret;
  }

  //summarise_buffers(pipe->video_buffers);
  while (component->buf_notempty == 0)
     pthread_cond_wait(&component->buf_notempty_cv,&component->buf_mutex);

  pthread_mutex_unlock(&component->buf_mutex);

  goto retry;

  /* We never get here, but keep GCC happy */
  return NULL;
}

OMX_ERRORTYPE omx_flush_tunnel(struct omx_component_t* source, int source_port, struct omx_component_t* sink, int sink_port)
{
  omx_send_command_and_wait(source,OMX_CommandFlush,source_port,NULL);
  omx_send_command_and_wait(sink,OMX_CommandFlush,sink_port,NULL);
}

OMX_ERRORTYPE omx_init_component(struct omx_pipeline_t* pipe, struct omx_component_t* component, char* compname)
{
  pthread_mutex_init(&component->cmd_queue_mutex, NULL);
  pthread_cond_init(&component->cmd_queue_count_cv,NULL);
  component->buf_notempty = 1;
  pthread_cond_init(&component->buf_notempty_cv,NULL);
  pthread_cond_init(&component->eos_cv,NULL);
  pthread_mutex_init(&component->eos_mutex,NULL);

  component->callbacks.EventHandler = omx_event_handler;
  component->callbacks.EmptyBufferDone = omx_empty_buffer_done;
  component->callbacks.FillBufferDone = omx_fill_buffer_done;

  component->pipe = pipe;

  /* Create OMX component */
  OERR(OMX_GetHandle(&component->h, compname, component, &component->callbacks));

  /* Disable all ports */
  omx_disable_all_ports(component);

}


OMX_ERRORTYPE omx_setup_pipeline(struct omx_pipeline_t* pipe, OMX_VIDEO_CODINGTYPE video_codec)
{
  OMX_VIDEO_PARAM_PORTFORMATTYPE format;
  OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;

  memset(pipe, 0, sizeof(struct omx_pipeline_t));

  omx_init_component(pipe, &pipe->video_decode, "OMX.broadcom.video_decode");
  omx_init_component(pipe, &pipe->video_render, "OMX.broadcom.video_render");
  omx_init_component(pipe, &pipe->clock, "OMX.broadcom.clock");

  OMX_INIT_STRUCTURE(cstate);
  cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
  cstate.nWaitMask = OMX_CLOCKPORT0;
  OERR(OMX_SetParameter(pipe->clock.h, OMX_IndexConfigTimeClockState, &cstate));

  omx_init_component(pipe, &pipe->video_scheduler, "OMX.broadcom.video_scheduler");

  /* Setup clock tunnel first */
  omx_send_command_and_wait(&pipe->clock, OMX_CommandStateSet, OMX_StateIdle, NULL);

  OERR(OMX_SetupTunnel(pipe->clock.h, 80, pipe->video_scheduler.h, 12));

  OERR(OMX_SendCommand(pipe->clock.h, OMX_CommandPortEnable, 80, NULL));
  OERR(OMX_SendCommand(pipe->video_scheduler.h, OMX_CommandPortEnable, 12, NULL));

  omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandStateSet, OMX_StateIdle, NULL);

  omx_send_command_and_wait(&pipe->clock, OMX_CommandStateSet, OMX_StateExecuting, NULL);

  /* Configure video_decoder */
  omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateIdle, NULL);

  OMX_INIT_STRUCTURE(format);
  format.nPortIndex = 130;
  format.eCompressionFormat = video_codec;

  OERR(OMX_SetParameter(pipe->video_decode.h, OMX_IndexParamVideoPortFormat, &format));

   /* Enable error concealment for H264 only - without this, HD channels don't work reliably */
  if (video_codec == OMX_VIDEO_CodingAVC) {
     OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ec;
     OMX_INIT_STRUCTURE(ec);
     ec.bStartWithValidFrame = OMX_FALSE;
     OERR(OMX_SetParameter(pipe->video_decode.h, OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ec));
  }


  /* Enable video decoder input port */
  omx_send_command_and_wait0(&pipe->video_decode, OMX_CommandPortEnable, 130, NULL);

  /* Allocate input buffers */
  pipe->video_decode.buffers = omx_alloc_buffers(pipe, pipe->video_decode.h, 130);

  /* Wait for input port to be enabled */
  omx_send_command_and_wait1(&pipe->video_decode, OMX_CommandPortEnable, 130, NULL);

  /* Change video_decode to OMX_StateExecuting */
  omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateExecuting, NULL);

  return OMX_ErrorNone;
}
