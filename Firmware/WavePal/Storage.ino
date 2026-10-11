/*
----------------------------------------------------------------------------

This file is part of the Pulse Pal Project
Copyright (C) 2026 Sanworks LLC, Rochester, NY, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


// The waveform data file on the microSD card, and refilling the playback buffers from it.
//
// Functions in this file:
//   haltWithMessage()
//   setupWaveFile()
//   chunkFilePosition()
//   readChunk()
//   refillPlaybackBuffers()
//   forgetBufferedBlock()
//   unloadWaveforms()
//
// DATA FILE
// WAVE_FILE_NAME holds the eight memory blocks, two per waveform (see "Waveforms" in Playback.ino), one region of
// WAVE_REGION_BYTES each, in block order. A block's samples are stored from the start of its region, as uint16 DAC
// codes, in chunks of BUFFER_SAMPLES. The file is created again at every startup, so waveforms do not survive a
// restart.
//
// The file is preallocated, so its clusters are contiguous and a seek to any chunk is quick. Only FAT16 and FAT32 cards
// are supported (Pulse Pal formats its card as FAT): on exFAT, SdFat cannot seek past the last byte written, even in a
// preallocated file, so a block could not be loaded before the blocks before it.

// Shows a message and stops. Used for errors at startup that leave the device unable to play anything.
void haltWithMessage(const char* line1, const char* line2) {
  write2Screen(line1, line2);
  while (true) {}
}

// Creates the waveform data file at startup. Halts with a message if the card cannot hold it.
void setupWaveFile() {
  if (sd.fatType() == FAT_TYPE_EXFAT) {
    haltWithMessage("SD card is exFAT", "Needs FAT32");
  }
  if (!sd.exists(WAVE_FOLDER)) {
    sd.mkdir(WAVE_FOLDER);
  }
  sd.remove(WAVE_FILE_NAME);
  waveFile = sd.open(WAVE_FILE_NAME, O_RDWR | O_CREAT);
  if (!waveFile) {
    haltWithMessage("Startup Failed:", "SD Card ERROR");
  }
  // preAllocate() needs WAVE_FILE_BYTES of contiguous free space, and sets the file size, so any chunk can be sought
  if (!waveFile.preAllocate(WAVE_FILE_BYTES) || (waveFile.fileSize() != WAVE_FILE_BYTES)) {
    haltWithMessage("SD card has no", "16MB free space");
  }
}

// Position in the data file of a chunk of a memory block's waveform
uint32_t chunkFilePosition(byte block, uint32_t chunk) {
  return (block * WAVE_REGION_BYTES) + (chunk * BUFFER_BYTES);
}

// Reads a chunk of a memory block's waveform into a playback buffer. Only the part of the chunk that belongs to the
// waveform is read. Returns false if the card could not be read.
bool readChunk(byte block, uint32_t chunk, uint16_t *buffer) {
  uint32_t nChunkSamples = nSamples[block] - (chunk * BUFFER_SAMPLES);
  if (nChunkSamples > BUFFER_SAMPLES) {
    nChunkSamples = BUFFER_SAMPLES;
  }
  if (!waveFile.seekSet(chunkFilePosition(block, chunk))) {
    return false;
  }
  return waveFile.read(buffer, nChunkSamples * 2) == (int)(nChunkSamples * 2);
}

// Reads the chunks the playing channels need next into their free playback buffers. loop() calls this on every pass,
// and op 76 calls it between chunks, so that playback continues while a waveform loads. A channel that is already
// waiting for its current chunk (an underrun) is served first, then the chunk after each channel's current one.
// See "How playback works" in Playback.ino.
void refillPlaybackBuffers() {
  for (byte pass = 0; pass < 2; pass++) { // Pass 0: the current chunk, if it is late. Pass 1: the next chunk.
    for (byte i = 0; i < N_CHANNELS; i++) {
      int32_t chunk = CHUNK_NONE;
      byte buffer = 0;
      byte block = NO_BLOCK;
      noInterrupts(); // The chunk, and the buffer it goes to, must not change between the decision and CHUNK_NONE below
      if (playing[i] && !stopAfterWrite[i]) {
        block = playBlock[i];
        uint32_t nChunks = (nSamples[block] + BUFFER_SAMPLES - 1) / BUFFER_SAMPLES;
        uint32_t wanted = playChunk[i];
        if (pass == 1) {
          wanted++;
          if ((wanted >= nChunks) && loopMode[i]) {
            wanted = 0; // After the last chunk comes chunk 0, which is always in the pre-buffer
          }
        }
        if ((wanted >= 1) && (wanted < nChunks)) {
          buffer = bufferForChunk(wanted);
          if ((bufferChunk[i][buffer] != (int32_t)wanted) || (bufferBlock[i][buffer] != block)) {
            // handler() is not playing from this buffer: it holds neither the current chunk nor, being the other
            // buffer, the one before it. Mark it empty until the read has finished.
            bufferChunk[i][buffer] = CHUNK_NONE;
            chunk = wanted;
          }
        }
      }
      interrupts();
      if (chunk != CHUNK_NONE) {
        if (readChunk(block, chunk, playbackBuffers[i][buffer])) {
          // handler() may play from it now. If a trigger restarted the channel on another memory block during the
          // read, the block does not match playBlock[], so handler() does not play it, and the next pass reads the
          // chunk it needs. Both are set together, since handler() checks both.
          noInterrupts();
          bufferBlock[i][buffer] = block;
          bufferChunk[i][buffer] = chunk;
          interrupts();
        } // If the read failed, the buffer stays empty, and the next pass tries again
      }
    }
  }
}

// Marks the playback buffers that hold chunks of a memory block as empty, before op 76 replaces the block's samples.
// Call with interrupts disabled, after stopping the channels that play the block.
void forgetBufferedBlock(byte block) {
  for (byte i = 0; i < N_CHANNELS; i++) {
    for (byte buffer = 0; buffer < 2; buffer++) {
      if (bufferBlock[i][buffer] == block) {
        bufferChunk[i][buffer] = CHUNK_NONE;
        bufferBlock[i][buffer] = NO_BLOCK;
      }
    }
  }
}

// Empties every memory block, e.g. when the output range changes, and discards loads stored for a param sync edge.
// Call with interrupts disabled, after stopping all channels.
void unloadWaveforms() {
  for (byte block = 0; block < N_MEMORY_BLOCKS; block++) {
    nSamples[block] = 0;
    forgetBufferedBlock(block);
  }
  for (byte w = 0; w < N_WAVEFORMS; w++) {
    loadStored[w] = false;
  }
}
