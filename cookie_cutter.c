// cookie_cutter.c
// SDL2 image crop tool with mouse drag/resize + real-time coordinate overlay
// Shows X Y (top-left) and W H (size) in top-left corner
//
// Compile (Linux/macOS):
//   cc -O2 -Wall cookie_cutter.c -o cookie_cutter \
//      $(sdl2-config --cflags --libs) -lSDL2_image -lSDL2_ttf -lm
//
// Windows (example with pkg-config style or manual paths):
//   gcc cookie_cutter.c -o cookie_cutter.exe \
//      -I/path/to/SDL2/include -I/path/to/SDL2_image/include -I/path/to/SDL2_ttf/include \
//      -L/path/to/SDL2/lib -L/path/to/SDL2_image/lib -L/path/to/SDL2_ttf/lib \
//      -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lSDL2_ttf
//
// Usage:
//   ./cookie_cutter image.png

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define WINDOW_W          1280
#define WINDOW_H           900
#define DEFAULT_SQUARE_SIZE 256
#define MIN_SQUARE_SIZE     32
#define PREVIEW_SIZE        256

typedef struct {
    int x, y;       // top-left in original image pixels
    int w, h;       // width & height (currently kept equal for square)
} CropRegion;

static void save_crop(SDL_Surface* src, const CropRegion* crop, const char* filename)
{
    if (crop->w <= 0 || crop->h <= 0) return;

    SDL_Rect srcrect = { crop->x, crop->y, crop->w, crop->h };
    SDL_Surface* cropped = SDL_CreateRGBSurfaceWithFormat(
        0, crop->w, crop->h, 32, SDL_PIXELFORMAT_RGBA32);

    if (!cropped) return;

    SDL_BlitSurface(src, &srcrect, cropped, NULL);

    if (IMG_SavePNG(cropped, filename) == 0) {
        printf("Saved: %s  (%d×%d)\n", filename, crop->w, crop->h);
    } else {
        fprintf(stderr, "Failed to save %s: %s\n", filename, IMG_GetError());
    }

    SDL_FreeSurface(cropped);
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image.png|jpg>\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];

    if (SDL_Init(SDL_INIT_VIDEO) < 0 || IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) == 0 || TTF_Init() == -1) {
        fprintf(stderr, "Initialization failed\n");
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Cookie Cutter - Drag to move, Drag bottom-right corner to resize, S = save, +/- = size, Q/Esc = quit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);

    if (!window) goto cleanup;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) goto cleanup;

    // Try to load a common system font (you can change path)
    TTF_Font* font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 18);
    if (!font) {
        font = TTF_OpenFont("/Library/Fonts/Arial.ttf", 18);           // macOS fallback
    }
    if (!font) {
        font = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", 18);      // Windows fallback
    }
    if (!font) {
        fprintf(stderr, "Warning: Could not load font - text overlay disabled\n");
    }

    SDL_Surface* surface = IMG_Load(input_path);
    if (!surface) {
        fprintf(stderr, "Cannot load image %s: %s\n", input_path, IMG_GetError());
        goto cleanup_font;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) goto cleanup_surface;

    int orig_w = surface->w;
    int orig_h = surface->h;

    CropRegion crop = {
        .x = (orig_w - DEFAULT_SQUARE_SIZE) / 2,
        .y = (orig_h - DEFAULT_SQUARE_SIZE) / 2,
        .w = DEFAULT_SQUARE_SIZE,
        .h = DEFAULT_SQUARE_SIZE
    };
    if (crop.x < 0) crop.x = 0;
    if (crop.y < 0) crop.y = 0;
    if (crop.x + crop.w > orig_w) crop.w = orig_w - crop.x;
    if (crop.y + crop.h > orig_h) crop.h = orig_h - crop.y;

    SDL_Texture* preview_tex = NULL;

    bool running = true;
    bool dragging = false;
    bool resizing = false;
    int drag_offset_x = 0, drag_offset_y = 0;

    SDL_Event event;
    while (running)
    {
        bool crop_changed = false;

        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym)
                    {
                        case SDLK_ESCAPE:
                        case SDLK_q:
                            running = false;
                            break;

                        case SDLK_s:
                        {
                            static int counter = 1;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "crop_%03d_%dx%d.png", counter++, crop.w, crop.h);
                            save_crop(surface, &crop, buf);
                        }
                        break;

                        case SDLK_LEFT:  if (crop.x > 0)               { crop.x--; crop_changed = true; } break;
                        case SDLK_RIGHT: if (crop.x + crop.w < orig_w) { crop.x++; crop_changed = true; } break;
                        case SDLK_UP:    if (crop.y > 0)               { crop.y--; crop_changed = true; } break;
                        case SDLK_DOWN:  if (crop.y + crop.h < orig_h) { crop.y++; crop_changed = true; } break;

                        case SDLK_EQUALS:
                        case SDLK_KP_PLUS:
                        case SDLK_PLUS:
                            if (crop.w < 2048 && crop.h < 2048) {
                                int cx = crop.x + crop.w / 2;
                                int cy = crop.y + crop.h / 2;
                                crop.w += 16;
                                crop.h += 16;
                                crop.x = cx - crop.w / 2;
                                crop.y = cy - crop.h / 2;
                                crop_changed = true;
                            }
                            break;

                        case SDLK_MINUS:
                        case SDLK_KP_MINUS:
                            if (crop.w > MIN_SQUARE_SIZE && crop.h > MIN_SQUARE_SIZE) {
                                int cx = crop.x + crop.w / 2;
                                int cy = crop.y + crop.h / 2;
                                crop.w -= 16;
                                crop.h -= 16;
                                crop.x = cx - crop.w / 2;
                                crop.y = cy - crop.h / 2;
                                crop_changed = true;
                            }
                            break;
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT)
                    {
                        int mx = event.button.x;
                        int my = event.button.y;

                        int disp_w, disp_h;
                        SDL_GetRendererOutputSize(renderer, &disp_w, &disp_h);

                        float scale_x = (float)disp_w / orig_w;
                        float scale_y = (float)disp_h / orig_h;
                        float scale = fmin(scale_x, scale_y);

                        int img_off_x = (disp_w - (int)(orig_w * scale)) / 2;
                        int img_off_y = (disp_h - (int)(orig_h * scale)) / 2;

                        float img_mx = (mx - img_off_x) / scale;
                        float img_my = (my - img_off_y) / scale;

                        if (img_mx >= crop.x && img_mx <= crop.x + crop.w &&
                            img_my >= crop.y && img_my <= crop.y + crop.h)
                        {
                            float dx = fabs(img_mx - (crop.x + crop.w));
                            float dy = fabs(img_my - (crop.y + crop.h));

                            if (dx < 24 && dy < 24) {
                                resizing = true;
                                drag_offset_x = (crop.x + crop.w) - img_mx;
                                drag_offset_y = (crop.y + crop.h) - img_my;
                            } else {
                                dragging = true;
                                drag_offset_x = img_mx - crop.x;
                                drag_offset_y = img_my - crop.y;
                            }
                        }
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        dragging = resizing = false;
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (dragging || resizing)
                    {
                        int mx = event.motion.x;
                        int my = event.motion.y;

                        int disp_w, disp_h;
                        SDL_GetRendererOutputSize(renderer, &disp_w, &disp_h);

                        float scale_x = (float)disp_w / orig_w;
                        float scale_y = (float)disp_h / orig_h;
                        float scale = fmin(scale_x, scale_y);

                        int img_off_x = (disp_w - (int)(orig_w * scale)) / 2;
                        int img_off_y = (disp_h - (int)(orig_h * scale)) / 2;

                        float img_mx = (mx - img_off_x) / scale;
                        float img_my = (my - img_off_y) / scale;

                        if (dragging)
                        {
                            crop.x = (int)(img_mx - drag_offset_x + 0.5f);
                            crop.y = (int)(img_my - drag_offset_y + 0.5f);

                            if (crop.x < 0) crop.x = 0;
                            if (crop.y < 0) crop.y = 0;
                            if (crop.x + crop.w > orig_w) crop.x = orig_w - crop.w;
                            if (crop.y + crop.h > orig_h) crop.y = orig_h - crop.h;

                            crop_changed = true;
                        }
                        else if (resizing)
                        {
                            int new_br_x = (int)(img_mx + drag_offset_x + 0.5f);
                            int new_br_y = (int)(img_my + drag_offset_y + 0.5f);

                            int new_w = new_br_x - crop.x;
                            int new_h = new_br_y - crop.y;

                            new_w = (int)fmax(new_w, MIN_SQUARE_SIZE);
                            new_h = (int)fmax(new_h, MIN_SQUARE_SIZE);
                            new_w = (int)fmin(new_w, orig_w - crop.x);
                            new_h = (int)fmin(new_h, orig_h - crop.y);

                            // Keep square for now (comment out next two lines to allow free rectangle)
                            new_w = new_h = fmin(new_w, new_h);

                            crop.w = new_w;
                            crop.h = new_h;
                            crop_changed = true;
                        }
                    }
                    break;
            }
        }

        // Update preview
        if (crop_changed || !preview_tex)
        {
            if (preview_tex) SDL_DestroyTexture(preview_tex);
            preview_tex = NULL;

            if (crop.w > 0 && crop.h > 0)
            {
                SDL_Rect srcrect = {crop.x, crop.y, crop.w, crop.h};
                SDL_Surface* crop_surf = SDL_CreateRGBSurfaceWithFormat(
                    0, crop.w, crop.h, 32, SDL_PIXELFORMAT_RGBA32);

                if (crop_surf)
                {
                    SDL_BlitSurface(surface, &srcrect, crop_surf, NULL);
                    preview_tex = SDL_CreateTextureFromSurface(renderer, crop_surf);
                    SDL_FreeSurface(crop_surf);
                }
            }
        }

        // ────────────────────────────────────────────────
        // Rendering
        // ────────────────────────────────────────────────
        SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
        SDL_RenderClear(renderer);

        int disp_w, disp_h;
        SDL_GetRendererOutputSize(renderer, &disp_w, &disp_h);

        float scale_x = (float)disp_w / orig_w;
        float scale_y = (float)disp_h / orig_h;
        float scale = fmin(scale_x, scale_y);

        int img_off_x = (disp_w - (int)(orig_w * scale)) / 2;
        int img_off_y = (disp_h - (int)(orig_h * scale)) / 2;

        SDL_Rect img_dst = { img_off_x, img_off_y, (int)(orig_w * scale), (int)(orig_h * scale) };
        SDL_RenderCopy(renderer, texture, NULL, &img_dst);

        // Crop overlay
        if (crop.w > 0 && crop.h > 0)
        {
            SDL_Rect crop_draw = {
                (int)(crop.x * scale) + img_off_x,
                (int)(crop.y * scale) + img_off_y,
                (int)(crop.w * scale),
                (int)(crop.h * scale)
            };

            // Dim background outside crop
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
            SDL_Rect shadows[4] = {
                {0, 0, disp_w, crop_draw.y},
                {0, crop_draw.y + crop_draw.h, disp_w, disp_h},
                {0, crop_draw.y, crop_draw.x, crop_draw.h},
                {crop_draw.x + crop_draw.w, crop_draw.y, disp_w - (crop_draw.x + crop_draw.w), crop_draw.h}
            };
            SDL_RenderFillRects(renderer, shadows, 4);

            // Green border
            SDL_SetRenderDrawColor(renderer, 80, 255, 120, 220);
            SDL_RenderDrawRect(renderer, &crop_draw);

            // Resize hint squares
            int csz = 14;
            SDL_SetRenderDrawColor(renderer, 255, 240, 60, 220);
            SDL_Rect corners[4] = {
                {crop_draw.x - csz/2, crop_draw.y - csz/2, csz, csz},
                {crop_draw.x + crop_draw.w - csz/2, crop_draw.y - csz/2, csz, csz},
                {crop_draw.x - csz/2, crop_draw.y + crop_draw.h - csz/2, csz, csz},
                {crop_draw.x + crop_draw.w - csz/2, crop_draw.y + crop_draw.h - csz/2, csz, csz}
            };
            for (int i = 0; i < 4; i++) SDL_RenderFillRect(renderer, &corners[i]);
        }

        // 1:1 Preview (bottom right)
        int preview_x = disp_w - PREVIEW_SIZE - 20;
        int preview_y = disp_h - PREVIEW_SIZE - 20;
        if (preview_tex)
        {
            SDL_Rect preview_rect = {preview_x, preview_y, PREVIEW_SIZE, PREVIEW_SIZE};
            SDL_RenderCopy(renderer, preview_tex, NULL, &preview_rect);
            SDL_SetRenderDrawColor(renderer, 200, 200, 220, 220);
            SDL_RenderDrawRect(renderer, &preview_rect);
        }

        // Overlay text: X Y W H
        if (font)
        {
            char text[128];
            snprintf(text, sizeof(text),
                     "X: %d   Y: %d    W: %d   H: %d    (S = save, +/- = resize, arrows = nudge)",
                     crop.x, crop.y, crop.w, crop.h);

            SDL_Color color = { 240, 240, 255, 255 };
            SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font, text, color);
            if (text_surf)
            {
                SDL_Texture* text_tex = SDL_CreateTextureFromSurface(renderer, text_surf);
                if (text_tex)
                {
                    int tw = text_surf->w;
                    int th = text_surf->h;
                    SDL_Rect dst = { 16, 16, tw, th };
                    SDL_RenderCopy(renderer, text_tex, NULL, &dst);
                    SDL_DestroyTexture(text_tex);
                }
                SDL_FreeSurface(text_surf);
            }
        }

        SDL_RenderPresent(renderer);
    }

    // Cleanup
    if (preview_tex) SDL_DestroyTexture(preview_tex);
    SDL_DestroyTexture(texture);

cleanup_surface:
    SDL_FreeSurface(surface);
cleanup_font:
    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
cleanup:
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
