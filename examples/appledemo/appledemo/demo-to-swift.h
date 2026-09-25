#ifndef DEMO_TO_SWIFT_H
#define DEMO_TO_SWIFT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void demoStart(const char *mp3path);
bool togglePlayPause(void);
bool toggleReverb(void);
bool toggleMicrophone(void);
void demoEnd(void);

#ifdef __cplusplus
}
#endif

#endif // DEMO_TO_SWIFT_H
