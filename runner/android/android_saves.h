#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* DKC1 USA v1.0 cartridge RAM (battery save). */
enum { kDkc1AndroidSramSize = 2048 };

/* Set the directory that holds save.srm (created if missing; its parent must
 * exist, e.g. Context.filesDir). Returns false if the path is too long or the
 * directory cannot be created. */
bool Dkc1AndroidSavesSetRoot(const char *root);

/* Load save.srm into ram. A missing file is a normal first launch. A file that
 * is the wrong size or unreadable falls back to save.srm.bak (written by older
 * builds and by this one); if that fails too the bad file is kept as
 * save.srm.corrupt and the game starts with a fresh save. Existing data is
 * never deleted. Returns false only for invalid arguments. */
bool Dkc1AndroidSramLoad(uint8_t *ram, size_t size);

/* Call once after every successful emulated frame (cheap: a 2 KB compare).
 * Writes the save once it has been unchanged for a short settle period, so a
 * half-copied save slot is never persisted, but never waits longer than a
 * few seconds. Failed writes are retried automatically. */
void Dkc1AndroidSramTick(const uint8_t *ram, size_t size);

/* Write now if the data differs from what is on disk. Use when pausing or
 * shutting down, and only from a point where emulation is not running.
 * Returns false if the write failed (the previous file is left intact). */
bool Dkc1AndroidSramFlush(const uint8_t *ram, size_t size);
