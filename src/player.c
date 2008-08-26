/* the Music Player Daemon (MPD)
 * Copyright (C) 2003-2007 by Warren Dukes (warren.dukes@gmail.com)
 * This project's homepage is: http://www.musicpd.org
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "player.h"
#include "path.h"
#include "command.h"
#include "log.h"
#include "playerData.h"
#include "ack.h"
#include "os_compat.h"
#include "main_notify.h"

static void playerCloseAudio(void);

void wakeup_player_nb(void)
{
	notify_signal(&pc.notify);
}

static void wakeup_player(void)
{
	notify_signal(&pc.notify);
	wait_main_task();
}

void player_sleep(void)
{
	notify_wait(&pc.notify);
}

static void * player_task(mpd_unused void *arg)
{
	notify_enter(&pc.notify);

	while (1) {
		if (pc.play) {
			decode();
			continue; /* decode() calls wakeup_main_task */
		} else if (pc.stop) {
			pc.stop = 0;
		} else if (pc.seek) {
			pc.seek = 0;
		} else if (pc.pause) {
			pc.pause = 0;
		} else if (pc.closeAudio) {
			closeAudioDevice();
			pc.closeAudio = 0;
		} else if (pc.lockQueue) {
			pc.queueLockState = PLAYER_QUEUE_LOCKED;
			pc.lockQueue = 0;
		} else if (pc.unlockQueue) {
			pc.queueLockState = PLAYER_QUEUE_UNLOCKED;
			pc.unlockQueue = 0;
		} else {
			player_sleep();
			continue;
		}
		/* we did something, tell the main task about it */
		wakeup_main_task();
	}
	return NULL;
}

void playerInit(void)
{
	pthread_attr_t attr;
	pthread_t player_thread;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&player_thread, &attr, player_task, NULL))
		FATAL("Failed to spawn player task: %s\n", strerror(errno));
}

int playerWait(int fd)
{
	if (playerStop(fd) < 0)
		return -1;

	playerCloseAudio();

	return 0;
}

static void set_current_song(Song *song)
{
	pc.fileTime = song->tag ? song->tag->time : 0;
	pc.next_song = song;
}

int playerPlay(int fd, Song * song)
{
	if (playerStop(fd) < 0)
		return -1;

	set_current_song(song);

	pc.play = 1;
	/* FIXME: _nb() variant is probably wrong here, and everywhere... */
	do { wakeup_player_nb(); } while (pc.play);

	return 0;
}

int playerStop(mpd_unused int fd)
{
	if (pc.state != PLAYER_STATE_STOP) {
		pc.stop = 1;
		do { wakeup_player(); } while (pc.stop);
	}

	pc.queueState = PLAYER_QUEUE_BLANK;
	playerQueueUnlock();

	return 0;
}

void playerKill(void) /* deprecated */
{
	playerPause(STDERR_FILENO);
}

int playerPause(mpd_unused int fd)
{
	if (pc.state != PLAYER_STATE_STOP) {
		pc.pause = 1;
		do { wakeup_player(); } while (pc.pause);
	}

	return 0;
}

int playerSetPause(int fd, int pause_flag)
{
	switch (pc.state) {
	case PLAYER_STATE_PLAY:
		if (pause_flag)
			playerPause(fd);
		break;
	case PLAYER_STATE_PAUSE:
		if (!pause_flag)
			playerPause(fd);
		break;
	}

	return 0;
}

int getPlayerElapsedTime(void)
{
	return (int)(pc.elapsedTime + 0.5);
}

unsigned long getPlayerBitRate(void)
{
	return pc.bitRate;
}

int getPlayerTotalTime(void)
{
	return (int)(pc.totalTime + 0.5);
}

int getPlayerState(void)
{
	return pc.state;
}

void clearPlayerError(void)
{
	pc.error = 0;
}

int getPlayerError(void)
{
	return pc.error;
}

char *getPlayerErrorStr(void)
{
	/* static OK here, only one user in main task */
	static char error[MPD_PATH_MAX + 64]; /* still too much */
	static const size_t errorlen = sizeof(error);
	char path_max_tmp[MPD_PATH_MAX];
	*error = '\0'; /* likely */

	switch (pc.error) {
	case PLAYER_ERROR_FILENOTFOUND:
		snprintf(error, errorlen,
			 "file \"%s\" does not exist or is inaccessible",
			 get_song_url(path_max_tmp, pc.errored_song));
		break;
	case PLAYER_ERROR_FILE:
		snprintf(error, errorlen, "problems decoding \"%s\"",
			 get_song_url(path_max_tmp, pc.errored_song));
		break;
	case PLAYER_ERROR_AUDIO:
		strcpy(error, "problems opening audio device");
		break;
	case PLAYER_ERROR_SYSTEM:
		strcpy(error, "system error occured");
		break;
	case PLAYER_ERROR_UNKTYPE:
		snprintf(error, errorlen, "file type of \"%s\" is unknown",
			 get_song_url(path_max_tmp, pc.errored_song));
	}
	return *error ? error : NULL;
}

static void playerCloseAudio(void)
{
	if (playerStop(STDERR_FILENO) < 0)
		return;
	pc.closeAudio = 1;
	do { wakeup_player(); } while (pc.closeAudio);
}

int queueSong(Song * song)
{
	if (pc.queueState == PLAYER_QUEUE_BLANK) {
		set_current_song(song);
		pc.queueState = PLAYER_QUEUE_FULL;
		return 0;
	}

	return -1;
}

int getPlayerQueueState(void)
{
	return pc.queueState;
}

void setQueueState(int queueState)
{
	pc.queueState = queueState;
	wakeup_player_nb();
}

void playerQueueLock(void)
{
	if (pc.queueLockState == PLAYER_QUEUE_UNLOCKED) {
		pc.lockQueue = 1;
		do { wakeup_player(); } while (pc.lockQueue);
	}
}

void playerQueueUnlock(void)
{
	if (pc.queueLockState == PLAYER_QUEUE_LOCKED) {
		pc.unlockQueue = 1;
		do { wakeup_player(); } while (pc.unlockQueue);
	}
}

int playerSeek(int fd, Song * song, float seek_time)
{
	assert(song != NULL);

	if (pc.state == PLAYER_STATE_STOP) {
		commandError(fd, ACK_ERROR_PLAYER_SYNC,
			     "player not currently playing");
		return -1;
	}

	if (pc.next_song != song)
		set_current_song(song);

	if (pc.error == PLAYER_ERROR_NOERROR) {
		pc.seekWhere = seek_time;
		pc.seek = 1;
		/* FIXME: _nb() is probably wrong here, too */
		do { wakeup_player_nb(); } while (pc.seek);
	}

	return 0;
}

float getPlayerCrossFade(void)
{
	return pc.crossFade;
}

void setPlayerCrossFade(float crossFadeInSeconds)
{
	if (crossFadeInSeconds < 0)
		crossFadeInSeconds = 0;
	pc.crossFade = crossFadeInSeconds;
}

void setPlayerSoftwareVolume(int volume)
{
	volume = (volume > 1000) ? 1000 : (volume < 0 ? 0 : volume);
	pc.softwareVolume = volume;
}

double getPlayerTotalPlayTime(void)
{
	return pc.totalPlayTime;
}

unsigned int getPlayerSampleRate(void)
{
	return pc.sampleRate;
}

int getPlayerBits(void)
{
	return pc.bits;
}

int getPlayerChannels(void)
{
	return pc.channels;
}

/* this actually creates a dupe of the current metadata */
Song *playerCurrentDecodeSong(void)
{
	return NULL;
}
