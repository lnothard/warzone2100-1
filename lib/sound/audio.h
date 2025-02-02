/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef __INCLUDED_LIB_SOUND_AUDIO_H__
#define __INCLUDED_LIB_SOUND_AUDIO_H__

enum class HRTFMode;
struct AUDIO_STREAM;
struct BaseObject;
typedef bool (* AUDIO_CALLBACK)(void *psObj);

bool audio_Init(AUDIO_CALLBACK pStopTrackCallback, HRTFMode hrtf, bool really_init);
void audio_Update();
bool audio_Shutdown();
bool audio_Disabled();

bool audio_LoadTrackFromFile(char szFileName[]);
unsigned int audio_SetTrackVals(const char *fileName, bool loop, unsigned int volume, unsigned int audibleRadius);
void audio_PlayBuildFailedOnce();
bool audio_PlayStaticTrack(int iX, int iY, int iTrack);
bool audio_PlayObjStaticTrack(BaseObject *psObj, int iTrack);
bool audio_PlayObjStaticTrackCallback(BaseObject *psObj, int iTrack, AUDIO_CALLBACK pUserCallback);
bool audio_PlayObjDynamicTrack(BaseObject *psObj, int iTrack, AUDIO_CALLBACK pUserCallback);
void audio_StopObjTrack(BaseObject *psObj, int iTrack);
void audio_PlayTrack(int iTrack);
void audio_PlayCallbackTrack(int iTrack, AUDIO_CALLBACK pUserCallback);
AUDIO_STREAM *audio_PlayStream(const char *fileName, float volume, void (*onFinished)(const void *), const void *user_data);
void audio_QueueTrack(int iTrack);
void audio_QueueTrackMinDelay(int iTrack, unsigned iMinDelay);
void audio_QueueTrackMinDelayPos(int iTrack, unsigned iMinDelay, int iX, int iY, int iZ);
void audio_QueueTrackGroup(int iTrack, int iGroup);
void audio_QueueTrackPos(int iTrack, int iX, int iY, int iZ);
void audio_QueueTrackGroupPos(int iTrack, int iGroup, int iX, int iY, int iZ);
bool audio_GetPreviousQueueTrackPos(int *iX, int *iY, int *iZ);
bool audio_GetPreviousQueueTrackRadarBlipPos(int *iX, int *iY);
void audio_PauseAll();
void audio_ResumeAll();
void audio_StopAll();

int audio_GetTrackID(const char *fileName);
void audio_RemoveObj(BaseObject const *psObj);
unsigned int audio_GetSampleQueueCount();
unsigned int audio_GetSampleListCount();
unsigned int sound_GetActiveSamplesCount();

#endif // __INCLUDED_LIB_SOUND_AUDIO_H__
