#include <stdlib.h>
#include "raylib.h"

Color *NewColor(int r, int g, int b) {
    Color *color = malloc(sizeof(*color));
    if (!color) return NULL;
    color->r = r;
    color->g = g;
    color->b = b;
    color->a = 255;
    return color;
}

void ClearBG(Color *color) {
    ClearBackground(*color);
}

void DrawRect(int posX, int posY, int width, int height, Color *color) {
    DrawRectangle(posX, posY, width, height, *color);
}
