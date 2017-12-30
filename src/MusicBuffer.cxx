/*
 * Copyright 2003-2017 The Music Player Daemon Project
 * http://www.musicpd.org
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "MusicBuffer.hxx"
#include "MusicChunk.hxx"

#include <assert.h>

MusicBuffer::MusicBuffer(unsigned num_chunks) noexcept
	:buffer(num_chunks) {
}

MusicChunkPtr
MusicBuffer::Allocate() noexcept
{
	const std::lock_guard<Mutex> protect(mutex);
	return MusicChunkPtr(buffer.Allocate(), MusicChunkDeleter(*this));
}

void
MusicBuffer::Return(MusicChunk *chunk) noexcept
{
	assert(chunk != nullptr);

	const std::lock_guard<Mutex> protect(mutex);

	assert(!chunk->other || !chunk->other->other);

	buffer.Free(chunk);
}
