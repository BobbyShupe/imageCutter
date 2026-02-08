// cookie_cutter.c
// SDL2-based cookie cutter tool
// Controls:
// - Drag inside      → move crop region
// - Drag bottom-right corner → resize (keeps square for now)
// - Arrow keys                  → move 1 pixel
// - Ctrl  + Arrow keys          → jump by current crop size (width/height)
// - Shift + Arrow keys          → resize by 1 pixel (grow/shrink, center preserved)
// - +/- keys                    → resize by 16 pixels (centered)
// - S key                       → save current crop as PNG
// - Real-time X:Y W:H overlay + 1:1 preview in bottom-right

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define WINDOW_W            1280
#define WINDOW_H             900
#define DEFAULT_SQUARE_SIZE  256
#define MIN_SQUARE_SIZE       32
#define PREVIEW_SIZE         256

typedef struct {
    int x, y;       // top-left in original image coordinates
    int w, h;       // width & height (currently forced square)
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

    if (SDL_Init(SDL_INIT_VIDEO) < 0 ||
        IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) == 0 ||
        TTF_Init() == -1)
    {
        fprintf(stderr, "SDL/IMG/TTF init failed\n");
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Cookie Cutter - Drag to move, Drag corner to resize, Arrows=1px move, Shift+Arrows=1px resize, Ctrl+Arrows=jump, +/-=16px resize, S=save",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);

    if (!window) goto cleanup;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) goto cleanup;

    TTF_Font* font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 18);
    if (!font) font = TTF_OpenFont("/Library/Fonts/Arial.ttf", 18);
    if (!font) font = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", 18);
    if (!font) fprintf(stderr, "Warning: font not loaded - no text overlay\n");

    SDL_Surface* surface = IMG_Load(input_path);
    if (!surface) {
        fprintf(stderr, "Failed to load %s: %s\n", input_path, IMG_GetError());
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
                {
                    bool ctrl  = (event.key.keysym.mod & KMOD_CTRL)  != 0;
                    bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;

                    switch (event.key.keysym.sym)
                    {
                        case SDLK_q:
                        case SDLK_ESCAPE:
                            running = false;
                            break;

                        case SDLK_s:
                        {
                            static int cnt = 1;
                            char fname[128];
                            snprintf(fname, sizeof(fname), "crop_%03d_%dx%d.png", cnt++, crop.w, crop.h);
                            save_crop(surface, &crop, fname);
                        }
                        break;

                        // ────────────────────────────────────────────────
                        // Arrow key combinations
                        // ────────────────────────────────────────────────
                        case SDLK_LEFT:
                            if (shift) {
                                // Shrink width by 1, try to keep center
                                if (crop.w > MIN_SQUARE_SIZE) {
                                    int old_center = crop.x + crop.w / 2;
                                    crop.w--;
                                    //crop.x = old_center - crop.w / 2;
                                    //if (crop.x < 0) crop.x = 0;
                                    //if (crop.x + crop.w > orig_w) crop.x = orig_w - crop.w;
                                    crop_changed = true;
                                }
                            }
                            else if (ctrl) {
                                if (crop.x >= crop.w) { crop.x -= crop.w; crop_changed = true; }
                            }
                            else {
                                if (crop.x > 0) { crop.x--; crop_changed = true; }
                            }
                            break;

                        case SDLK_RIGHT:
                            if (shift) {
                                // Grow width by 1, try to keep center
                                if (crop.w < orig_w && crop.x + crop.w < orig_w) {
                                    int old_center = crop.x + crop.w / 2;
                                    crop.w++;
                                    //crop.x = old_center - crop.w / 2;
                                    //if (crop.x < 0) crop.x = 0;
                                    //if (crop.x + crop.w > orig_w) crop.x = orig_w - crop.w;
                                    crop_changed = true;
                                }
                            }
                            else if (ctrl) {
                                if (crop.x + crop.w + crop.w <= orig_w) { crop.x += crop.w; crop_changed = true; }
                            }
                            else {
                                if (crop.x + crop.w < orig_w) { crop.x++; crop_changed = true; }
                            }
                            break;

                        case SDLK_UP:
                            if (shift) {
                                // Shrink height by 1, try to keep center
                                if (crop.h > MIN_SQUARE_SIZE) {
                                    int old_center = crop.y + crop.h / 2;
                                    crop.h--;
                                    //crop.y = old_center - crop.h / 2;
                                    //if (crop.y < 0) crop.y = 0;
                                    //if (crop.y + crop.h > orig_h) crop.y = orig_h - crop.h;
                                    crop_changed = true;
                                }
                            }
                            else if (ctrl) {
                                if (crop.y >= crop.h) { crop.y -= crop.h; crop_changed = true; }
                            }
                            else {
                                if (crop.y > 0) { crop.y--; crop_changed = true; }
                            }
                            break;

                        case SDLK_DOWN:
                            if (shift) {
                                // Grow height by 1, try to keep center
                                if (crop.h < orig_h && crop.y + crop.h < orig_h) {
                                    int old_center = crop.y + crop.h / 2;
                                    crop.h++;
                                    //crop.y = old_center - crop.h / 2;
                                    //if (crop.y < 0) crop.y = 0;
                                    //if (crop.y + crop.h > orig_h) crop.y = orig_h - crop.h;
                                    crop_changed = true;
                                }
                            }
                            else if (ctrl) {
                                if (crop.y + crop.h + crop.h <= orig_h) { crop.y += crop.h; crop_changed = true; }
                            }
                            else {
                                if (crop.y + crop.h < orig_h) { crop.y++; crop_changed = true; }
                            }
                            break;

                        case SDLK_EQUALS:
                        case SDLK_KP_PLUS:
                        case SDLK_PLUS:
                            if (crop.w < 2048 && crop.h < 2048) {
                                int cx = crop.x + crop.w / 2;
                                int cy = crop.y + crop.h / 2;
                                crop.w += 16;
                                crop.h += 16;
                                //crop.x = cx - crop.w / 2;
                                //crop.y = cy - crop.h / 2;
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
                                //crop.x = cx - crop.w / 2;
                                //crop.y = cy - crop.h / 2;
                                crop_changed = true;
                            }
                            break;
                    }
                }
                break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT)
                    {
                        int mx = event.button.x, my = event.button.y;
                        int rw, rh;
                        SDL_GetRendererOutputSize(renderer, &rw, &rh);

                        float sx = (float)rw / orig_w;
                        float sy = (float)rh / orig_h;
                        float s  = fmin(sx, sy);

                        int ox = (rw - (int)(orig_w * s)) / 2;
                        int oy = (rh - (int)(orig_h * s)) / 2;

                        float ix = (mx - ox) / s;
                        float iy = (my - oy) / s;

                        if (ix >= crop.x && ix <= crop.x + crop.w &&
                            iy >= crop.y && iy <= crop.y + crop.h)
                        {
                            float dx = fabs(ix - (crop.x + crop.w));
                            float dy = fabs(iy - (crop.y + crop.h));

                            if (dx < 24 && dy < 24) {
                                resizing = true;
                                drag_offset_x = (crop.x + crop.w) - ix;
                                drag_offset_y = (crop.y + crop.h) - iy;
                            } else {
                                dragging = true;
                                drag_offset_x = ix - crop.x;
                                drag_offset_y = iy - crop.y;
                            }
                        }
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT)
                        dragging = resizing = false;
                    break;

                case SDL_MOUSEMOTION:
                    if (dragging || resizing)
                    {
                        int mx = event.motion.x, my = event.motion.y;
                        int rw, rh;
                        SDL_GetRendererOutputSize(renderer, &rw, &rh);

                        float sx = (float)rw / orig_w;
                        float sy = (float)rh / orig_h;
                        float s  = fmin(sx, sy);

                        int ox = (rw - (int)(orig_w * s)) / 2;
                        int oy = (rh - (int)(orig_h * s)) / 2;

                        float ix = (mx - ox) / s;
                        float iy = (my - oy) / s;

                        if (dragging)
                        {
                            crop.x = (int)(ix - drag_offset_x + 0.5f);
                            crop.y = (int)(iy - drag_offset_y + 0.5f);
                            if (crop.x < 0) crop.x = 0;
                            if (crop.y < 0) crop.y = 0;
                            if (crop.x + crop.w > orig_w) crop.x = orig_w - crop.w;
                            if (crop.y + crop.h > orig_h) crop.y = orig_h - crop.h;
                            crop_changed = true;
                        }
                        else if (resizing)
                        {
                            int brx = (int)(ix + drag_offset_x + 0.5f);
                            int bry = (int)(iy + drag_offset_y + 0.5f);
                            int nw = brx - crop.x;
                            int nh = bry - crop.y;
                            nw = fmax(nw, MIN_SQUARE_SIZE);
                            nh = fmax(nh, MIN_SQUARE_SIZE);
                            nw = fmin(nw, orig_w - crop.x);
                            nh = fmin(nh, orig_h - crop.y);
                            // force square — comment next line for free aspect ratio
                            nw = nh = fmin(nw, nh);
                            crop.w = nw;
                            crop.h = nh;
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
                SDL_Rect r = {crop.x, crop.y, crop.w, crop.h};
                SDL_Surface* cs = SDL_CreateRGBSurfaceWithFormat(0, crop.w, crop.h, 32, SDL_PIXELFORMAT_RGBA32);
                if (cs)
                {
                    SDL_BlitSurface(surface, &r, cs, NULL);
                    preview_tex = SDL_CreateTextureFromSurface(renderer, cs);
                    SDL_FreeSurface(cs);
                }
            }
        }

        // ──────────────────────────────────────────────── Render ────────────────────────────────────────────────
        SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
        SDL_RenderClear(renderer);

        int rw, rh;
        SDL_GetRendererOutputSize(renderer, &rw, &rh);

        float sx = (float)rw / orig_w;
        float sy = (float)rh / orig_h;
        float s  = fmin(sx, sy);

        int ox = (rw - (int)(orig_w * s)) / 2;
        int oy = (rh - (int)(orig_h * s)) / 2;

        SDL_Rect imgdst = {ox, oy, (int)(orig_w * s), (int)(orig_h * s)};
        SDL_RenderCopy(renderer, texture, NULL, &imgdst);

        if (crop.w > 0 && crop.h > 0)
        {
            SDL_Rect cdst = {
                (int)(crop.x * s) + ox,
                (int)(crop.y * s) + oy,
                (int)(crop.w * s),
                (int)(crop.h * s)
            };

            // Dim outside
            SDL_SetRenderDrawColor(renderer, 0,0,0,140);
            SDL_Rect sh[4] = {
                {0, 0, rw, cdst.y},
                {0, cdst.y + cdst.h, rw, rh},
                {0, cdst.y, cdst.x, cdst.h},
                {cdst.x + cdst.w, cdst.y, rw - (cdst.x + cdst.w), cdst.h}
            };
            SDL_RenderFillRects(renderer, sh, 4);

            // Border
            SDL_SetRenderDrawColor(renderer, 80, 255, 120, 220);
            //SDL_RenderDrawRect(renderer, &cdst);

            // Corner handles
            int csz = 14;
            SDL_SetRenderDrawColor(renderer, 255, 240, 60, 220);
            SDL_Rect corners[4] = {
                {cdst.x - csz/2, cdst.y - csz/2, csz, csz},
                {cdst.x + cdst.w - csz/2, cdst.y - csz/2, csz, csz},
                {cdst.x - csz/2, cdst.y + cdst.h - csz/2, csz, csz},
                {cdst.x + cdst.w - csz/2, cdst.y + cdst.h - csz/2, csz, csz}
            };
            for (int i = 0; i < 4; i++) SDL_RenderFillRect(renderer, &corners[i]);
        }

        // 1:1 preview
        int px = rw - PREVIEW_SIZE - 20;
        int py = rh - PREVIEW_SIZE - 20;
        if (preview_tex)
        {
            SDL_Rect prect = {px, py, PREVIEW_SIZE, PREVIEW_SIZE};
            SDL_RenderCopy(renderer, preview_tex, NULL, &prect);
            SDL_SetRenderDrawColor(renderer, 200,200,220,220);
            //SDL_RenderDrawRect(renderer, &prect);
        }

        // Text overlay
        if (font)
        {
            char buf[180];
            snprintf(buf, sizeof(buf),
                     "X: %d  Y: %d   W: %d  H: %d   (S=save  Arrows=move 1px  Shift+Arrows=resize 1px  Ctrl+Arrows=jump  +/-=16px)",
                     crop.x, crop.y, crop.w, crop.h);

            SDL_Color col = {240,240,255,255};
            SDL_Surface* ts = TTF_RenderUTF8_Blended(font, buf, col);
            if (ts)
            {
                SDL_Texture* tt = SDL_CreateTextureFromSurface(renderer, ts);
                if (tt)
                {
                    int tw = ts->w, th = ts->h;
                    SDL_Rect tdst = {16, 16, tw, th};
                    SDL_RenderCopy(renderer, tt, NULL, &tdst);
                    SDL_DestroyTexture(tt);
                }
                SDL_FreeSurface(ts);
            }
        }

        SDL_RenderPresent(renderer);
    }

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
