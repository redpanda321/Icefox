/*
 * Copyright © 2012 Mozilla Foundation
 *
 * This program is made available under an ISC-style license.  See the
 * accompanying file LICENSE for details.
 */
#undef NDEBUG
#include "cubeb/cubeb.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <SLES/OpenSLES.h>

struct cubeb {
  void * lib;
  SLInterfaceID SL_IID_BUFFERQUEUE;
  SLInterfaceID SL_IID_PLAY;
  SLObjectItf engObj;
  SLEngineItf eng;
  SLObjectItf outmixObj;
};

#define NBUFS 4

struct cubeb_stream {
  struct cubeb * context;
  SLObjectItf playerObj;
  SLPlayItf play;
  SLBufferQueueItf bufq;
  void *queuebuf[NBUFS];
  int queuebuf_idx;
  long queuebuf_len;
  long bytespersec;
  long framesize;
  int draining;

  cubeb_data_callback data_callback;
  cubeb_state_callback state_callback;
  void * user_ptr;
};

static void
bufferqueue_callback(SLBufferQueueItf caller, struct cubeb_stream *stm)
{
  SLBufferQueueState state;
  (*stm->bufq)->GetState(stm->bufq, &state);

  if (stm->draining) {
    if (!state.count) {
      stm->draining = 0;
      stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_DRAINED);
    }
    return;
  }

  if (state.count > 1)
    return;

  SLuint32 i;
  for (i = state.count; i < NBUFS; i++) {
    void *buf = stm->queuebuf[stm->queuebuf_idx];
    long written = stm->data_callback(stm, stm->user_ptr,
                                      buf, stm->queuebuf_len / stm->framesize);
    if (written == CUBEB_ERROR) {
      (*stm->play)->SetPlayState(stm->play, SL_PLAYSTATE_STOPPED);
      return;
    }

    (*stm->bufq)->Enqueue(stm->bufq, buf, written * stm->framesize);
    stm->queuebuf_idx = (stm->queuebuf_idx + 1) % NBUFS;

    if ((written * stm->framesize) < stm->queuebuf_len) {
      stm->draining = 1;
      return;
    }
  }
}

int
cubeb_init(cubeb ** context, char const * context_name)
{
  cubeb * ctx;

  *context = NULL;

  ctx = calloc(1, sizeof(*ctx));
  assert(ctx);

  ctx->lib = dlopen("libOpenSLES.so", RTLD_LAZY);
  if (!ctx->lib) {
    free(ctx);
    return CUBEB_ERROR;
  }

  typedef SLresult (*slCreateEngine_t)(SLObjectItf *,
                                       SLuint32,
                                       const SLEngineOption *,
                                       SLuint32,
                                       const SLInterfaceID *,
                                       const SLboolean *);
  slCreateEngine_t f_slCreateEngine =
    (slCreateEngine_t)dlsym(ctx->lib, "slCreateEngine");
  SLInterfaceID SL_IID_ENGINE = *(SLInterfaceID *)dlsym(ctx->lib, "SL_IID_ENGINE");
  SLInterfaceID SL_IID_OUTPUTMIX = *(SLInterfaceID *)dlsym(ctx->lib, "SL_IID_OUTPUTMIX");
  ctx->SL_IID_BUFFERQUEUE = *(SLInterfaceID *)dlsym(ctx->lib, "SL_IID_BUFFERQUEUE");
  ctx->SL_IID_PLAY = *(SLInterfaceID *)dlsym(ctx->lib, "SL_IID_PLAY");
  if (!f_slCreateEngine ||
      !SL_IID_ENGINE ||
      !SL_IID_OUTPUTMIX ||
      !ctx->SL_IID_BUFFERQUEUE ||
      !ctx->SL_IID_PLAY) {
    cubeb_destroy(ctx);
    return CUBEB_ERROR;
  }


  const SLEngineOption opt[] = {{SL_ENGINEOPTION_THREADSAFE, SL_BOOLEAN_TRUE}};

  SLresult res;
  res = f_slCreateEngine(&ctx->engObj, 1, opt, 0, NULL, NULL);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_destroy(ctx);
    return CUBEB_ERROR;
  }

  res = (*ctx->engObj)->Realize(ctx->engObj, SL_BOOLEAN_FALSE);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_destroy(ctx);
    return CUBEB_ERROR;
  }

  res = (*ctx->engObj)->GetInterface(ctx->engObj, SL_IID_ENGINE, &ctx->eng);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_destroy(ctx);
    return CUBEB_ERROR;
  }

  const SLInterfaceID idsom[] = {SL_IID_OUTPUTMIX};
  const SLboolean reqom[] = {SL_BOOLEAN_TRUE};
  res = (*ctx->eng)->CreateOutputMix(ctx->eng, &ctx->outmixObj, 1, idsom, reqom);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_destroy(ctx);
    return CUBEB_ERROR;
  }

  res = (*ctx->outmixObj)->Realize(ctx->outmixObj, SL_BOOLEAN_FALSE);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_destroy(ctx);
    return CUBEB_ERROR;
  }

  *context = ctx;

  return CUBEB_OK;
}

char const *
cubeb_get_backend_id(cubeb * ctx)
{
  return "opensl";
}

void
cubeb_destroy(cubeb * ctx)
{
  dlclose(ctx->lib);
  if (ctx->outmixObj)
    (*ctx->outmixObj)->Destroy(ctx->outmixObj);
  if (ctx->engObj)
    (*ctx->engObj)->Destroy(ctx->engObj);
  free(ctx);
}

int
cubeb_stream_init(cubeb * ctx, cubeb_stream ** stream, char const * stream_name,
                  cubeb_stream_params stream_params, unsigned int latency,
                  cubeb_data_callback data_callback, cubeb_state_callback state_callback,
                  void * user_ptr)
{
  cubeb_stream * stm;

  assert(ctx);

  *stream = NULL;

  if (stream_params.rate < 8000 || stream_params.rate > 48000 ||
      stream_params.channels < 1 || stream_params.channels > 32 ||
      latency < 1 || latency > 2000) {
    return CUBEB_ERROR_INVALID_FORMAT;
  }

  SLDataFormat_PCM format;

  format.formatType = SL_DATAFORMAT_PCM;
  format.numChannels = stream_params.channels;
  // samplesPerSec is in milliHertz
  format.samplesPerSec = stream_params.rate * 1000;
  format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
  format.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
  format.channelMask = stream_params.channels == 1 ?
                       SL_SPEAKER_FRONT_CENTER :
                       SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;

  switch (stream_params.format) {
  case CUBEB_SAMPLE_S16LE:
    format.endianness = SL_BYTEORDER_LITTLEENDIAN;
    break;
  case CUBEB_SAMPLE_S16BE:
    format.endianness = SL_BYTEORDER_BIGENDIAN;
    break;
  default:
    return CUBEB_ERROR_INVALID_FORMAT;
  }

  stm = calloc(1, sizeof(*stm));
  assert(stm);

  stm->context = ctx;
  stm->data_callback = data_callback;
  stm->state_callback = state_callback;
  stm->user_ptr = user_ptr;

  stm->framesize = stream_params.channels * sizeof(int16_t);
  stm->bytespersec = stream_params.rate * stm->framesize;
  stm->queuebuf_len = (stm->bytespersec * latency) / (1000 * NBUFS);
  stm->queuebuf_len += stm->framesize - (stm->queuebuf_len % stm->framesize);
  int i;
  for (i = 0; i < NBUFS; i++) {
    stm->queuebuf[i] = malloc(stm->queuebuf_len);
    assert(stm->queuebuf[i]);
  }

  SLDataLocator_BufferQueue loc_bufq;
  loc_bufq.locatorType = SL_DATALOCATOR_BUFFERQUEUE;
  loc_bufq.numBuffers = NBUFS;
  SLDataSource source;
  source.pLocator = &loc_bufq;
  source.pFormat = &format;

  SLDataLocator_OutputMix loc_outmix;
  loc_outmix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
  loc_outmix.outputMix = ctx->outmixObj;
  SLDataSink sink;
  sink.pLocator = &loc_outmix;
  sink.pFormat = NULL;

  const SLInterfaceID ids[] = {ctx->SL_IID_BUFFERQUEUE};
  const SLboolean req[] = {SL_BOOLEAN_TRUE};
  SLresult res = (*ctx->eng)->CreateAudioPlayer(ctx->eng, &stm->playerObj,
                                                &source, &sink, 1, ids, req);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_stream_destroy(stm);
    return CUBEB_ERROR;
  }

  res = (*stm->playerObj)->Realize(stm->playerObj, SL_BOOLEAN_FALSE);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_stream_destroy(stm);
    return CUBEB_ERROR;
  }

  res = (*stm->playerObj)->GetInterface(stm->playerObj, ctx->SL_IID_PLAY, &stm->play);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_stream_destroy(stm);
    return CUBEB_ERROR;
  }

  res = (*stm->playerObj)->GetInterface(stm->playerObj, ctx->SL_IID_BUFFERQUEUE,
                                    &stm->bufq);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_stream_destroy(stm);
    return CUBEB_ERROR;
  }

  res = (*stm->bufq)->RegisterCallback(stm->bufq, bufferqueue_callback, stm);
  if (res != SL_RESULT_SUCCESS) {
    cubeb_stream_destroy(stm);
    return CUBEB_ERROR;
  }

  *stream = stm;

  return CUBEB_OK;
}

void
cubeb_stream_destroy(cubeb_stream * stm)
{
  if (stm->playerObj)
    (*stm->playerObj)->Destroy(stm->playerObj);
  free(stm);
}

int
cubeb_stream_start(cubeb_stream * stm)
{
  SLresult res = (*stm->play)->SetPlayState(stm->play, SL_PLAYSTATE_PLAYING);
  if (res != SL_RESULT_SUCCESS)
    return CUBEB_ERROR;
  stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_STARTED);
  bufferqueue_callback(NULL, stm);
  return CUBEB_OK;
}

int
cubeb_stream_stop(cubeb_stream * stm)
{
  SLresult res = (*stm->play)->SetPlayState(stm->play, SL_PLAYSTATE_PAUSED);
  if (res != SL_RESULT_SUCCESS)
    return CUBEB_ERROR;
  stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_STOPPED);
  return CUBEB_OK;
}

int
cubeb_stream_get_position(cubeb_stream * stm, uint64_t * position)
{
  SLmillisecond msec;
  SLresult res = (*stm->play)->GetPosition(stm->play, &msec);
  if (res != SL_RESULT_SUCCESS)
    return CUBEB_ERROR;
  *position = (stm->bytespersec / (1000 * stm->framesize)) * msec;
  return CUBEB_OK;
}

