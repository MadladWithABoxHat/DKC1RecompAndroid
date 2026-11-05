#pragma once

#include <stdbool.h>

bool Dkc1AndroidAudioInit(void);
void Dkc1AndroidAudioShutdown(void);
void Dkc1AndroidAudioRenderFrame(void);
void Dkc1AndroidAudioSetPaused(bool paused);
