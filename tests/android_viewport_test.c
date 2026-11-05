/* Host test: gcc -std=c11 -Irunner -Irunner/android tests/android_viewport_test.c -lm */
#include "android_viewport.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  /* Values the project's own docs and iOS host use. */
  assert(Dkc1AndroidRenderWidth(1920, 1080) == 342);  /* 16:9  */
  assert(Dkc1AndroidRenderWidth(2560, 1600) == 308);  /* 16:10 */
  assert(Dkc1AndroidRenderWidth(2796, 1290) == 418);  /* 19.5:9 */
  assert(Dkc1AndroidRenderWidth(2400, 1080) == 428);  /* 20:9  */
  /* Narrow screens stay native; absurdly wide ones stop at PPU capacity. */
  assert(Dkc1AndroidRenderWidth(1024, 768) == 256);
  assert(Dkc1AndroidRenderWidth(800, 800) == 256);
  assert(Dkc1AndroidRenderWidth(6000, 1000) == kDkc1VideoMaxWidth);
  /* Bad input falls back to native. */
  assert(Dkc1AndroidRenderWidth(0, 0) == 256);
  assert(Dkc1AndroidRenderWidth(-5, 100) == 256);
  /* Always even and inside capacity. */
  for (int w = 400; w <= 4000; w += 7)
    for (int h = 300; h <= 2000; h += 11) {
      int s = Dkc1AndroidRenderWidth(w, h);
      assert(s % 2 == 0 && s >= 256 && s <= kDkc1VideoMaxWidth);
    }
  puts("VIEWPORT TESTS PASSED");
  return 0;
}
