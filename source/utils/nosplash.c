/*
 * vitaGL renders a spinning "vitaGL" splashscreen between vglInit() and the
 * first vglSwapBuffers() (see splashscreen.o in libvitaGL.a). We do not want
 * it: defining the symbols it exports here means the linker never pulls
 * splashscreen.o out of the archive, and vgl.o / gxm.o simply see an
 * inactive splash. The screen stays black until the game draws its first
 * frame.
 */
#include <stddef.h>
#include <stdint.h>

uint8_t is_splashscreen_active = 0;
int splash_mutex[2] = {0, 0};

void invoke_splashscreen(void) {}
void clear_splashscreen(void) {}

void *vglGetCaveBuffer(size_t *sz) {
    if (sz) *sz = 0;
    return NULL;
}
