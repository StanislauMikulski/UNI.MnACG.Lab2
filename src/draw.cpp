#include "draw.h"

#include <cmath>

#define RGB32(r, g, b) static_cast<uint32_t>((((static_cast<uint32_t>(b) << 8) | g) << 8) | r)

void put_pixel32(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    assert(NULL != surface);
    assert(x < SCREEN_WIDTH);
    assert(y < SCREEN_HEIGHT);

    Uint32 *pixels = (Uint32 *)surface->pixels;
    pixels[(y * surface->w) + x] = pixel;
}

Uint32 get_pixel32(SDL_Surface *surface, int x, int y)
{
    assert(NULL != surface);
    assert(x < SCREEN_WIDTH);
    assert(y < SCREEN_HEIGHT);

    Uint32 *pixels = (Uint32 *)surface->pixels;
    return pixels[(y * surface->w) + x];
}

float posX = SCREEN_WIDTH / 2;
float posY = SCREEN_HEIGHT / 2;
float scale = 200;
float angle = 0;
int count = 20;
int m = 4;
float mu = 0.1;

float PI = 3.14159265;

bool nWasPressed = false;
bool mWasPressed = false;
bool vWasPressed = false;
bool cWasPressed = false;

void put_pixel_safe(SDL_Surface *s, int x, int y, Uint32 color)
{
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT)
        put_pixel32(s, x, y, color);
}

void line(SDL_Surface *s, int x1, int y1, int x2, int y2, Uint32 color)
{
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = 1;
    int sy = 1;
    if (x2 < x1) sx = -1;
    if (y2 < y1) sy = -1;

    if (dy <= dx)
    {
        int d = (dy << 1) - dx;
        int d1 = dy << 1;
        int d2 = (dy - dx) << 1;
        put_pixel_safe(s, x1, y1, color);
        int x = x1 + sx;
        int y = y1;
        for (int i = 1; i <= dx; i++)
        {
            if (d > 0)
            {
                d += d2;
                y += sy;
            }
            else
                d += d1;
            put_pixel_safe(s, x, y, color);
            x += sx;
        }
    }
    else
    {
        int d = (dx << 1) - dy;
        int d1 = dx << 1;
        int d2 = (dx - dy) << 1;
        put_pixel_safe(s, x1, y1, color);
        int x = x1;
        int y = y1 + sy;
        for (int i = 1; i <= dy; i++)
        {
            if (d > 0)
            {
                d += d2;
                x += sx;
            }
            else
                d += d1;
            put_pixel_safe(s, x, y, color);
            y += sy;
        }
    }
}

void mul(float a[3][3], float b[3][3], float result[3][3])
{
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            result[i][j] = 0;
            for (int k = 0; k < 3; k++)
                result[i][j] = result[i][j] + a[i][k] * b[k][j];
        }
    }
}

void draw(SDL_Surface *s)
{
    SDL_FillRect(s, NULL, 0);

    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    if (keys[SDL_SCANCODE_W]) posY = posY - 3;
    if (keys[SDL_SCANCODE_S]) posY = posY + 3;
    if (keys[SDL_SCANCODE_A]) posX = posX - 3;
    if (keys[SDL_SCANCODE_D]) posX = posX + 3;

    if (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS]) scale = scale * 1.02;
    if (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]) scale = scale / 1.02;

    if (keys[SDL_SCANCODE_Q]) angle = angle - 0.02;
    if (keys[SDL_SCANCODE_E]) angle = angle + 0.02;

    if (keys[SDL_SCANCODE_N] && nWasPressed == false)
    {
        if (count < 100) count = count + 1;
    }
    if (keys[SDL_SCANCODE_M] && mWasPressed == false)
    {
        if (count > 1) count = count - 1;
    }
    nWasPressed = keys[SDL_SCANCODE_N];
    mWasPressed = keys[SDL_SCANCODE_M];

    if (keys[SDL_SCANCODE_V] && vWasPressed == false)
    {
        if (m < 20) m = m + 1;
    }
    if (keys[SDL_SCANCODE_C] && cWasPressed == false)
    {
        if (m > 3) m = m - 1;
    }
    vWasPressed = keys[SDL_SCANCODE_V];
    cWasPressed = keys[SDL_SCANCODE_C];

    float c = cos(angle);
    float sn = sin(angle);

    float T[3][3] = {
        {1, 0, posX},
        {0, 1, posY},
        {0, 0, 1}
    };

    float R[3][3] = {
        {c, -sn, 0},
        {sn, c, 0},
        {0, 0, 1}
    };

    float S[3][3] = {
        {scale, 0, 0},
        {0, scale, 0},
        {0, 0, 1}
    };

    float TR[3][3];
    float M[3][3];
    mul(T, R, TR);
    mul(TR, S, M);

    float px[20];
    float py[20];
    for (int i = 0; i < m; i++)
    {
        float a = 2 * PI * i / m;
        px[i] = cos(a);
        py[i] = sin(a);
    }

    for (int k = 0; k < count; k++)
    {
        for (int i = 0; i < m; i++)
        {
            int next = (i + 1) % m;

            float x1 = M[0][0] * px[i] + M[0][1] * py[i] + M[0][2];
            float y1 = M[1][0] * px[i] + M[1][1] * py[i] + M[1][2];
            float x2 = M[0][0] * px[next] + M[0][1] * py[next] + M[0][2];
            float y2 = M[1][0] * px[next] + M[1][1] * py[next] + M[1][2];

            line(s, (int)x1, (int)y1, (int)x2, (int)y2, RGB32(0, 255, 0));
        }

        float nx[20];
        float ny[20];
        for (int i = 0; i < m; i++)
        {
            int next = (i + 1) % m;
            nx[i] = (1 - mu) * px[i] + mu * px[next];
            ny[i] = (1 - mu) * py[i] + mu * py[next];
        }

        for (int i = 0; i < m; i++)
        {
            px[i] = nx[i];
            py[i] = ny[i];
        }
    }
}
