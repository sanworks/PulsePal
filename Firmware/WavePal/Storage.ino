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
//   unloadWaveforms()
//
// DATA FILE
// WAVE_FILE_NAME holds the four waveforms, one region of WAVE_REGION_BYTES per output channel, in channel order. A
// waveform's samples are stored from the start of its region, as uint16 DAC codes, in blocks ("chunks") of
// BUFFER_SAMPLES. The file is created again at every startup, so waveforms do not survive a restart.
//
// The file is preallocated, so its clusters are contiguous and a seek to any block is quick. Only FAT16 and FAT32 cards
// are supported (Pulse Pal formats its card as FAT): on exFAT, SdFat cannot seek past the last byte written, even in a
// preallocated file, so a channel could not be loaded before the channels before it.

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
  // preAllocate() needs WAVE_FILE_BYTES of contiguous free space, and sets the file size, so any block can be sought
  if (!waveFile.preAllocate(WAVE_FILE_BYTES) || (waveFile.fileSize() != WAVE_FILE_BYTES)) {
    haltWithMessage("SD card has no", "8MB free space");
  }
}

// Position in the data file of a chunk of a channel's waveform
uint32_t chunkFilePosition(byte channel, uint32_t chunk) {
  return (channel * WAVE_REGION_BYTES) + (chunk * BUFFER_BYTES);
}

// Reads a chunk of a channel's waveform into a playback buffer. Only the part of the chunk that belongs to the
// waveform is read. Returns false if the card could not be read.
bool readChunk(byte channel, uint32_t chunk, uint16_t *buffer) {
  uint32_t nChunkSamples = nSamples[channel] - (chunk * BUFFER_SAMPLES);
  if (nChunkSamples > BUFFER_SAMPLES) {
    nChunkSamples = BUFFER_SAMPLES;
  }
  if (!waveFile.seekSet(chunkFilePosition(channel, chunk))) {
    return false;
  }
  return waveFile.read(buffer, nChunkSamples * 2) == (int)(nChunkSamples * 2);
}

// Reads the chunks the playing channels need next into their free playback buffers. loop() calls this on every pass,
// and op 76 calls it between blocks, so that playback continues while a waveform loads. A channel that is already
// waiting for its current chunk (an underrun) is served first, then the chunk after each channel's current one.
// See "How playback works" in Playback.ino.
void refillPlaybackBuffers() {
  for (byte pass = 0; pass < 2; pass++) { // Pass 0: the current chunk, if it is late. Pass 1: the next chunk.
    for (byte i = 0; i < N_CHANNELS; i++) {
      int32_t chunk = CHUNK_NONE;
      byte buffer = 0;
      noInterrupts(); // The chunk, and the buffer it goes to, must not change between the decision and CHUNK_NONE below
      if (playing[i] && !stopAfterWrite[i]) {
        uint32_t nChunks = (nSamples[i] + BUFFER_SAMPLES - 1) / BUFFER_SAMPLES;
        uint32_t wanted = playChunk[i];
        if (pass == 1) {
          wanted++;
          if ((wanted >= nChunks) && loopMode[i]) {
            wanted = 0; // After the last chunk comes chunk 0, which is always in the pre-buffer
          }
        }
        if ((wanted >= 1) && (wanted < nChunks)) {
          buffer = bufferForChunk(wanted);
          if (bufferChunk[i][buffer] != (int32_t)wanted) {
            // handler() is not playing from this buffer: it holds neither the current chunk nor, being the other
            // buffer, the one before it. Mark it empty until the read has finished.
            bufferChunk[i][buffer] = CHUNK_NONE;
            chunk = wanted;
          }
        }
      }
      interrupts();
      if (chunk != CHUNK_NONE) {
        if (readChunk(i, chunk, playbackBuffers[i][buffer])) {
          bufferChunk[i][buffer] = chunk; // handler() may play from it now
        } // If the read failed, the buffer stays empty, and the next pass tries again
      }
    }
  }
}

// Marks every channel as having no waveform, e.g. when the output range changes. Call with interrupts disabled.
void unloadWaveforms() {
  for (byte i = 0; i < N_CHANNELS; i++) {
    nSamples[i] = 0;
    bufferChunk[i][0] = CHUNK_NONE;
    bufferChunk[i][1] = CHUNK_NONE;
  }
}
