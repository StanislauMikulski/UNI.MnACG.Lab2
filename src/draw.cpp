#include "draw.h"

#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

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

// ============================================================================
//  Растеризация
// ============================================================================

// put_pixel32 с отсечением по границам экрана
static void put_pixel_safe(SDL_Surface *s, int x, int y, Uint32 color)
{
    if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;
    put_pixel32(s, x, y, color);
}

// Алгоритм Брезенхейма для произвольного отрезка (целочисленный)
static void bresenham(SDL_Surface *s, int x1, int y1, int x2, int y2, Uint32 color)
{
    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    int sx = x2 >= x1 ? 1 : -1;
    int sy = y2 >= y1 ? 1 : -1;

    if (dy <= dx)
    {
        int d  = (dy << 1) - dx;
        int d1 = dy << 1;
        int d2 = (dy - dx) << 1;
        put_pixel_safe(s, x1, y1, color);
        for (int x = x1 + sx, y = y1, i = 1; i <= dx; i++, x += sx)
        {
            if (d > 0) { d += d2; y += sy; }
            else         d += d1;
            put_pixel_safe(s, x, y, color);
        }
    }
    else
    {
        int d  = (dx << 1) - dy;
        int d1 = dx << 1;
        int d2 = (dx - dy) << 1;
        put_pixel_safe(s, x1, y1, color);
        for (int x = x1, y = y1 + sy, i = 1; i <= dy; i++, y += sy)
        {
            if (d > 0) { d += d2; x += sx; }
            else         d += d1;
            put_pixel_safe(s, x, y, color);
        }
    }
}

// ============================================================================
//  Состояние программы
// ============================================================================

static bool      g_inited     = false;
static glm::mat4 g_M(1.0f);            // ОБЩАЯ матрица аффинных преобразований (S, R, T)
static glm::vec2 g_pivot(0.0f);        // точка, вокруг которой вращаем/масштабируем (экранные коорд.)
static float     g_totalScale = 1.0f;  // накопленный масштаб (только для ограничения)

static int   g_sides     = 4;     // число вершин многоугольника (3..MAX_SIDES)
static int   g_layers    = 25;    // число вложенных фигур
static bool  g_ordered   = true;  // режим "упорядоченного" узора (mu считается по формуле)
static int   g_k         = 1;     // множитель k: суммарный поворот = k * pi / m
static float g_mu        = 0.1f;  // mu в ручном режиме
static bool  g_dashed    = false; // пунктир
static int   g_dashParts = 8;     // на сколько равных частей делится отрезок (чётное)

static const int   MAX_SIDES  = 16;
static const int   MAX_LAYERS = 300;
static const float PI         = 3.14159265358979f;

static Uint8  g_prevKeys[SDL_NUM_SCANCODES] = {0};
static Uint32 g_prevMouse = 0;
static Uint32 g_prevTicks = 0;

// Вращение вокруг точки P:  T(P) * R * T(-P)
static glm::mat4 rotateAbout(const glm::vec2 &P, float ang)
{
    return glm::translate(glm::mat4(1.0f), glm::vec3(P, 0.0f)) *
           glm::rotate(glm::mat4(1.0f), ang, glm::vec3(0, 0, 1)) *
           glm::translate(glm::mat4(1.0f), glm::vec3(-P, 0.0f));
}

// Масштабирование относительно точки P
static glm::mat4 scaleAbout(const glm::vec2 &P, float k)
{
    return glm::translate(glm::mat4(1.0f), glm::vec3(P, 0.0f)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(k, k, 1.0f)) *
           glm::translate(glm::mat4(1.0f), glm::vec3(-P, 0.0f));
}

// ============================================================================
//  Отрезок по параметрическому уравнению  P(t) = (1-t)*A + t*B,  t in [0,1]
//  Пунктир: отрезок делится на D равных частей, чётные рисуются, нечётные нет.
// ============================================================================
static void draw_segment(SDL_Surface *s, glm::vec2 a, glm::vec2 b, Uint32 color)
{
    // защита от неадекватно больших координат (переполнение int)
    if (std::fabs(a.x) > 1e6f || std::fabs(a.y) > 1e6f ||
        std::fabs(b.x) > 1e6f || std::fabs(b.y) > 1e6f)
        return;

    if (!g_dashed)
    {
        bresenham(s, (int)std::lround(a.x), (int)std::lround(a.y),
                     (int)std::lround(b.x), (int)std::lround(b.y), color);
        return;
    }

    const int D = g_dashParts;
    for (int j = 0; j < D; j += 2)          // только чётные части
    {
        float t0 = (float)j       / D;
        float t1 = (float)(j + 1) / D;
        glm::vec2 p0 = (1.0f - t0) * a + t0 * b;
        glm::vec2 p1 = (1.0f - t1) * a + t1 * b;
        bresenham(s, (int)std::lround(p0.x), (int)std::lround(p0.y),
                     (int)std::lround(p1.x), (int)std::lround(p1.y), color);
    }
}

// ============================================================================
//  Значение mu, при котором внутренний многоугольник "упорядочен"
//  (после n шагов повёрнут на k*pi/m относительно внешнего).
//  Поворот за один шаг для правильного m-угольника (theta = 2pi/m):
//     tg(phi) = mu*sin(theta) / ((1-mu) + mu*cos(theta))
//  Отсюда  mu = t / (sin(theta) + t*(1-cos(theta))),  t = tg(k*pi/(m*n)).
//  Для m = 4 получается формулу из методички: mu = t / (t + 1).
// ============================================================================
static float compute_mu()
{
    if (!g_ordered)
        return g_mu;

    int n = g_layers - 1;
    if (n < 1)
        return 0.5f;

    float theta  = 2.0f * PI / g_sides;
    float target = g_k * PI / g_sides;
    float t      = std::tan(target / n);
    float mu     = t / (std::sin(theta) + t * (1.0f - std::cos(theta)));

    if (!(mu > 0.001f)) mu = 0.001f;
    if (mu > 0.999f)    mu = 0.999f;
    return mu;
}

// ============================================================================
//  Обработка ввода (опрос состояния клавиатуры/мыши каждый кадр)
// ============================================================================
//  W A S D      - перенос (T)
//  - / +        - масштаб (S), относительно точки вращения
//  Q / E        - вращение против / по часовой стрелке (R), вокруг точки вращения
//  ЛКМ          - задать точку вращения (узор при этом не смещается)
//  N / M        - число вложенных фигур +1 / -1
//  Вверх / Вниз - число вершин многоугольника +1 / -1
//  Влево/Вправо - множитель k упорядоченного узора
//  T            - упорядоченный режим вкл/выкл
//  F / G        - mu -/+ (в ручном режиме)
//  Z            - пунктир вкл/выкл
//  V / B        - число частей пунктира -2 / +2
//  Пробел       - сброс
// ============================================================================
static void reset_view()
{
    glm::vec2 c(SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f);
    g_M          = glm::translate(glm::mat4(1.0f), glm::vec3(c, 0.0f));
    g_pivot      = c;
    g_totalScale = 1.0f;
}

static void handle_input()
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    Uint32 now = SDL_GetTicks();
    float dt = (g_prevTicks == 0) ? 0.016f : (now - g_prevTicks) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    g_prevTicks = now;

    auto held    = [&](SDL_Scancode sc) { return keys[sc] != 0; };
    auto pressed = [&](SDL_Scancode sc) { return keys[sc] && !g_prevKeys[sc]; };

    const float moveSpeed   = 250.0f;   // пикс/с
    const float rotSpeed    = 1.2f;     // рад/с
    const float scaleSpeed  = 1.0f;     // множитель в степени dt

    // ---- T: перенос. Обновляем общую матрицу, а не пересчитываем цепочку ----
    glm::vec3 d(0.0f);
    if (held(SDL_SCANCODE_W)) d.y -= moveSpeed * dt;
    if (held(SDL_SCANCODE_S)) d.y += moveSpeed * dt;
    if (held(SDL_SCANCODE_A)) d.x -= moveSpeed * dt;
    if (held(SDL_SCANCODE_D)) d.x += moveSpeed * dt;
    if (d.x != 0.0f || d.y != 0.0f)
    {
        g_M = glm::translate(glm::mat4(1.0f), d) * g_M;
        g_pivot += glm::vec2(d.x, d.y);      // точка вращения "едет" вместе с фигурой
    }

    // ---- R: вращение вокруг выбранной точки ----
    float ang = 0.0f;
    if (held(SDL_SCANCODE_Q)) ang -= rotSpeed * dt;   // против часовой (ось Y вниз)
    if (held(SDL_SCANCODE_E)) ang += rotSpeed * dt;   // по часовой
    if (ang != 0.0f)
        g_M = rotateAbout(g_pivot, ang) * g_M;

    // ---- S: масштаб ----
    float k = 1.0f;
    if (held(SDL_SCANCODE_EQUALS) || held(SDL_SCANCODE_KP_PLUS))
        k *= std::pow(2.0f, scaleSpeed * dt);
    if (held(SDL_SCANCODE_MINUS) || held(SDL_SCANCODE_KP_MINUS))
        k /= std::pow(2.0f, scaleSpeed * dt);
    if (k != 1.0f)
    {
        float ns = g_totalScale * k;
        if (ns >= 0.05f && ns <= 20.0f)
        {
            g_totalScale = ns;
            g_M = scaleAbout(g_pivot, k) * g_M;
        }
    }

    // ---- Мышь: выбор точки вращения. Матрица не меняется -> узор не смещается ----
    int mx, my;
    Uint32 mb = SDL_GetMouseState(&mx, &my);
    if ((mb & SDL_BUTTON(SDL_BUTTON_LEFT)) && !(g_prevMouse & SDL_BUTTON(SDL_BUTTON_LEFT)))
        g_pivot = glm::vec2((float)mx, (float)my);
    g_prevMouse = mb;

    // ---- Число вложенных фигур ----
    if (pressed(SDL_SCANCODE_N) && g_layers < MAX_LAYERS) g_layers++;
    if (pressed(SDL_SCANCODE_M) && g_layers > 1)          g_layers--;

    // ---- Число вершин ----
    if (pressed(SDL_SCANCODE_UP)   && g_sides < MAX_SIDES) g_sides++;
    if (pressed(SDL_SCANCODE_DOWN) && g_sides > 3)         g_sides--;

    // ---- Параметры узора ----
    if (pressed(SDL_SCANCODE_RIGHT) && g_k < 16) g_k++;
    if (pressed(SDL_SCANCODE_LEFT)  && g_k > 1)  g_k--;
    if (pressed(SDL_SCANCODE_T)) g_ordered = !g_ordered;
    if (held(SDL_SCANCODE_F)) g_mu = std::max(0.005f, g_mu - 0.2f * dt);
    if (held(SDL_SCANCODE_G)) g_mu = std::min(0.995f, g_mu + 0.2f * dt);

    // ---- Пунктир ----
    if (pressed(SDL_SCANCODE_Z)) g_dashed = !g_dashed;
    if (pressed(SDL_SCANCODE_B) && g_dashParts < 64) g_dashParts += 2;
    if (pressed(SDL_SCANCODE_V) && g_dashParts > 2)  g_dashParts -= 2;

    // ---- Сброс ----
    if (pressed(SDL_SCANCODE_SPACE)) reset_view();

    std::memcpy(g_prevKeys, keys, sizeof(g_prevKeys));
}

// ============================================================================
//  Отрисовка
// ============================================================================
void draw(SDL_Surface *s)
{
    if (!g_inited)
    {
        reset_view();
        g_inited = true;
    }

    handle_input();

    // очистка кадра
    SDL_FillRect(s, NULL, 0x00000000);

    const int   m  = g_sides;
    const float R0 = 0.45f * std::min(SCREEN_WIDTH, SCREEN_HEIGHT);   // радиус исходной фигуры
    const float mu = compute_mu();

    // Исходный m-угольник: вершины на окружности, разделённой на m секторов.
    // Координаты локальные (центр фигуры в (0,0)); на экран переводит матрица g_M.
    std::vector<glm::vec2> cur(m), next(m);
    for (int i = 0; i < m; i++)
    {
        float a = -PI / 2 + 2.0f * PI * i / m;
        cur[i] = glm::vec2(R0 * std::cos(a), R0 * std::sin(a));
    }

    std::vector<glm::vec2> scr(m);

    for (int l = 0; l < g_layers; l++)
    {
        // цвет слоя: плавный градиент от красного к синему
        float f = (g_layers > 1) ? (float)l / (g_layers - 1) : 0.0f;
        // Поверхность создана с масками R=0x00FF0000, G=0x0000FF00, B=0x000000FF,
        // т.е. формат 0x00RRGGBB. Макрос RGB32 кладёт R в младший байт (0x00BBGGRR),
        // поэтому для красного/синего собираем цвет вручную.
        Uint32 color = ((Uint32)(255 * (1.0f - f)) << 16) | (160u << 8) | (Uint32)(255 * f);

        // одна общая матрица для всех вершин
        for (int i = 0; i < m; i++)
        {
            glm::vec4 p = g_M * glm::vec4(cur[i], 0.0f, 1.0f);
            scr[i] = glm::vec2(p.x, p.y);
        }

        for (int i = 0; i < m; i++)
            draw_segment(s, scr[i], scr[(i + 1) % m], color);

        // Следующий вложенный многоугольник:
        //   P'_i = (1 - mu) * P_i + mu * P_{i+1}   (параметрическое уравнение стороны)
        for (int i = 0; i < m; i++)
            next[i] = (1.0f - mu) * cur[i] + mu * cur[(i + 1) % m];
        cur.swap(next);
    }

    // маркер точки вращения (белый крестик)
    int px = (int)std::lround(g_pivot.x), py = (int)std::lround(g_pivot.y);
    for (int i = -4; i <= 4; i++)
    {
        put_pixel_safe(s, px + i, py, 0x00FFFFFF);
        put_pixel_safe(s, px, py + i, 0x00FFFFFF);
    }
}
