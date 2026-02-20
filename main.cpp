#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── Constants ────────────────────────────────────────────────────────────────
const int WINDOW_W    = 1100;
const int WINDOW_H    = 650;
const int PALETTE_W   = 200;
const int WORKSPACE_X = PALETTE_W;
const int WORKSPACE_W = 500;
const int STAGE_X     = PALETTE_W + WORKSPACE_W;
const int STAGE_W     = WINDOW_W - STAGE_X;
const int BLOCK_WIDTH  = 170;
const int BLOCK_HEIGHT = 38;
const int BLOCK_GAP    = 10;

// ─── Types ────────────────────────────────────────────────────────────────────
enum BlockCategory { MOTION, EVENT, LOOKS };

enum BlockType {
    EVENT_FLAG,
    MOVE_X,   // مثبت = راست، منفی = چپ
    MOVE_Y,   // مثبت = پایین، منفی = بالا
    LOOKS_SHOW, LOOKS_HIDE
};

struct Block {
    SDL_Rect      rect;
    BlockCategory cat;
    BlockType     type;
    SDL_Color     color;
    int           steps;      // مقدار (می‌تواند منفی باشد)
};

// ─── Globals ──────────────────────────────────────────────────────────────────
SDL_Texture* spriteTexture = nullptr;
float spriteX = 200.0f, spriteY = 200.0f;
bool  spriteVisible = true;

std::vector<Block> palette;
std::vector<Block> workspace;
int  draggingIdx = -1;
SDL_Point dragOffset{};

// script engine
bool running     = false;
int  scriptStep  = 0;
std::chrono::steady_clock::time_point lastStepTime;
int  highlightIdx = -1;

// keyboard editing
int  editingIdx  = -1;   // index در workspace که داریم edit می‌کنیم (-1 = هیچ)
std::string editBuffer;  // رشته موقت ورودی

TTF_Font* font = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────────────
void FillCircle(SDL_Renderer* r, int cx, int cy, int rad) {
    for (int dy = -rad; dy <= rad; dy++)
        for (int dx = -rad; dx <= rad; dx++)
            if (dx*dx + dy*dy <= rad*rad)
                SDL_RenderDrawPoint(r, cx+dx, cy+dy);
}

void DrawText(SDL_Renderer* r, const std::string& txt, int x, int y,
              SDL_Color c = {255,255,255,255}) {
    if (!font || txt.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, txt.c_str(), c);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst{x, y, s->w, s->h};
    SDL_FreeSurface(s);
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

// رسم badge عددی (با حالت edit فعال)
void DrawValueBadge(SDL_Renderer* r, int x, int y, int steps,
                    bool editing, const std::string& buf) {
    // پس‌زمینه badge
    SDL_Color bg = editing ? SDL_Color{255, 230, 100, 255}
                           : SDL_Color{255, 255, 255, 255};
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_Rect badge{x, y, 52, 20};
    SDL_RenderFillRect(r, &badge);
    SDL_SetRenderDrawColor(r, editing ? 200 : 100, 100, 100, 255);
    SDL_RenderDrawRect(r, &badge);

    std::string shown = editing ? (buf.empty() ? "_" : buf + "_")
                                : std::to_string(steps);
    DrawText(r, shown, x + 4, y + 2, {30, 30, 30, 255});
}

// ─── Block Drawing ────────────────────────────────────────────────────────────
void DrawBlock(SDL_Renderer* r, const Block& b, bool highlight, bool editing,
               const std::string& buf) {
    SDL_Color c = b.color;
    if (highlight) { c.r = std::min(255,c.r+60); c.g = std::min(255,c.g+60); }

    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(r, &b.rect);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 80);
    SDL_RenderDrawRect(r, &b.rect);

    std::string label;
    switch (b.type) {
        case EVENT_FLAG:  label = "When Flag Clicked"; break;
        case MOVE_X:      label = "Move X:";           break;
        case MOVE_Y:      label = "Move Y:";           break;
        case LOOKS_SHOW:  label = "Show";               break;
        case LOOKS_HIDE:  label = "Hide";               break;
    }
    DrawText(r, label, b.rect.x + 8, b.rect.y + 11);

    // badge برای بلوک‌های حرکتی
    if (b.type == MOVE_X || b.type == MOVE_Y) {
        int bx = b.rect.x + b.rect.w - 58;
        int by = b.rect.y + (b.rect.h - 20) / 2;
        DrawValueBadge(r, bx, by, b.steps, editing, buf);
    }
}

// ─── Palette Setup ────────────────────────────────────────────────────────────
void BuildPalette() {
    SDL_Color colEvent  = {255, 165,   0, 255};
    SDL_Color colMotion = { 70, 130, 180, 255};
    SDL_Color colLooks  = {180,  80, 200, 255};

    int x = 15, y = 10;
    auto add = [&](BlockCategory cat, BlockType type, SDL_Color col, int steps=10) {
        palette.push_back({{x, y, BLOCK_WIDTH, BLOCK_HEIGHT}, cat, type, col, steps});
        y += BLOCK_HEIGHT + BLOCK_GAP;
    };

    add(EVENT,  EVENT_FLAG, colEvent,   0);
    y += 8;
    add(MOTION, MOVE_X,     colMotion, 10);
    add(MOTION, MOVE_Y,     colMotion, 10);
    y += 8;
    add(LOOKS,  LOOKS_SHOW, colLooks,   0);
    add(LOOKS,  LOOKS_HIDE, colLooks,   0);
}

// ─── Script Engine ────────────────────────────────────────────────────────────
void StartScript() {
    for (int i = 0; i < (int)workspace.size(); i++) {
        if (workspace[i].type == EVENT_FLAG) {
            running      = true;
            scriptStep   = i + 1;
            lastStepTime = std::chrono::steady_clock::now();
            highlightIdx = i;
            return;
        }
    }
}

void UpdateScript() {
    if (!running) return;
    if (scriptStep >= (int)workspace.size()) {
        running = false; highlightIdx = -1; return;
    }
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastStepTime).count() < 400) return;
    lastStepTime = now;

    Block& cur = workspace[scriptStep];
    highlightIdx = scriptStep;

    switch (cur.type) {
        case MOVE_X: spriteX += (float)cur.steps; break;
        case MOVE_Y: spriteY += (float)cur.steps; break;  // مثبت = پایین
        case LOOKS_SHOW: spriteVisible = true;  break;
        case LOOKS_HIDE: spriteVisible = false; break;
        default: break;
    }
    scriptStep++;
}

// ─── Rendering ────────────────────────────────────────────────────────────────
void DrawSprite(SDL_Renderer* r) {
    if (!spriteVisible) return;
    int sx = STAGE_X + (int)spriteX;
    int sy = (int)spriteY;
    if (spriteTexture) {
        SDL_Rect dst{sx-25, sy-25, 50, 50};
        SDL_RenderCopy(r, spriteTexture, nullptr, &dst);
    } else {
        SDL_SetRenderDrawColor(r, 255, 140, 0, 255);
        FillCircle(r, sx, sy, 25);
    }
}

void Render(SDL_Renderer* r) {
    SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
    SDL_RenderClear(r);

    // palette bg
    SDL_SetRenderDrawColor(r, 45, 45, 60, 255);
    SDL_Rect palRect{0, 0, PALETTE_W, WINDOW_H};
    SDL_RenderFillRect(r, &palRect);

    // workspace bg
    SDL_SetRenderDrawColor(r, 55, 55, 70, 255);
    SDL_Rect wsRect{WORKSPACE_X, 0, WORKSPACE_W, WINDOW_H};
    SDL_RenderFillRect(r, &wsRect);

    // stage bg
    SDL_SetRenderDrawColor(r, 220, 230, 245, 255);
    SDL_Rect stageRect{STAGE_X, 0, STAGE_W, WINDOW_H - 30};
    SDL_RenderFillRect(r, &stageRect);

    // palette
    for (auto& b : palette)
        DrawBlock(r, b, false, false, "");

    // workspace
    for (int i = 0; i < (int)workspace.size(); i++)
        DrawBlock(r, workspace[i],
                  i == highlightIdx,
                  i == editingIdx,
                  i == editingIdx ? editBuffer : "");

    DrawSprite(r);

    // info bar
    SDL_SetRenderDrawColor(r, 40, 40, 55, 255);
    SDL_Rect infoBar{STAGE_X, WINDOW_H-30, STAGE_W, 30};
    SDL_RenderFillRect(r, &infoBar);
    DrawText(r, "X:" + std::to_string((int)spriteX) +
                " Y:" + std::to_string((int)spriteY),
             STAGE_X+8, WINDOW_H-22, {200,220,255,255});

    // hint برای edit
    if (editingIdx >= 0)
        DrawText(r, "Type number + Enter (ESC to cancel)",
                 WORKSPACE_X+10, WINDOW_H-22, {255,220,100,255});

    // green flag
    SDL_SetRenderDrawColor(r, 50, 200, 80, 255);
    SDL_Rect flagBtn{STAGE_X+10, 10, 60, 30};
    SDL_RenderFillRect(r, &flagBtn);
    DrawText(r, "> GO", STAGE_X+18, 17);

    SDL_RenderPresent(r);
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(int, char**) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

    SDL_Window*   win = SDL_CreateWindow("Scratch Clone",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* r = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // fonts
    const char* fonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc"
    };
    for (auto fp : fonts) {
        font = TTF_OpenFont(fp, 13);
        if (font) break;
    }

    // sprite image
    const char* paths[] = {"sprite.jpg","assets/sprite.jpg",
                            "../sprite.jpg","cmake-build-debug/sprite.jpg"};
    for (auto p : paths) {
        spriteTexture = IMG_LoadTexture(r, p);
        if (spriteTexture) { std::cout << "Loaded: " << p << "\n"; break; }
    }
    if (!spriteTexture) std::cout << "Using circle fallback\n";

    BuildPalette();
    SDL_StartTextInput();

    bool quit = false;
    SDL_Event ev;
    SDL_Rect flagBtn{STAGE_X+10, 10, 60, 30};

    while (!quit) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { quit = true; break; }

            // ── Text Input (وقتی داریم عدد می‌نویسیم) ──────────────────
            if (ev.type == SDL_TEXTINPUT && editingIdx >= 0) {
                char ch = ev.text.text[0];
                // اجازه: ارقام و یک بار منفی در ابتدا
                if ((ch >= '0' && ch <= '9') ||
                    (ch == '-' && editBuffer.empty())) {
                    if (editBuffer.size() < 5)
                        editBuffer += ch;
                }
                continue;
            }

            // ── Keyboard ─────────────────────────────────────────────────
            if (ev.type == SDL_KEYDOWN) {
                if (editingIdx >= 0) {
                    if (ev.key.keysym.sym == SDLK_RETURN ||
                        ev.key.keysym.sym == SDLK_KP_ENTER) {
                        // تأیید
                        if (!editBuffer.empty() && editBuffer != "-") {
                            try {
                                int val = std::stoi(editBuffer);
                                workspace[editingIdx].steps = val;
                            } catch (...) {}
                        }
                        editingIdx = -1;
                        editBuffer.clear();
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        editingIdx = -1;
                        editBuffer.clear();
                    } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                        if (!editBuffer.empty())
                            editBuffer.pop_back();
                    }
                } else {
                    // Space = اجرای اسکریپت (shortcut)
                    if (ev.key.keysym.sym == SDLK_SPACE)
                        StartScript();
                }
                continue;
            }

            // ── Mouse Button Down ─────────────────────────────────────────
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                SDL_Point mp{ev.button.x, ev.button.y};

                // اگر در حال edit هستیم و کلیک جای دیگری شد، لغو
                if (editingIdx >= 0) {
                    editingIdx = -1;
                    editBuffer.clear();
                }

                // پرچم سبز
                if (SDL_PointInRect(&mp, &flagBtn)) { StartScript(); continue; }

                // کلیک روی badge بلوک‌های حرکتی در workspace
                bool clickedBadge = false;
                for (int i = 0; i < (int)workspace.size(); i++) {
                    Block& blk = workspace[i];
                    if (blk.type != MOVE_X && blk.type != MOVE_Y) continue;
                    int bx = blk.rect.x + blk.rect.w - 58;
                    int by = blk.rect.y + (blk.rect.h - 20) / 2;
                    SDL_Rect badgeRect{bx, by, 52, 20};
                    if (SDL_PointInRect(&mp, &badgeRect)) {
                        editingIdx  = i;
                        editBuffer  = std::to_string(blk.steps);
                        clickedBadge = true;
                        break;
                    }
                }
                if (clickedBadge) continue;

                // drag از palette
                for (int i = 0; i < (int)palette.size(); i++) {
                    if (SDL_PointInRect(&mp, &palette[i].rect)) {
                        Block b = palette[i];
                        b.rect.x = mp.x - BLOCK_WIDTH/2;
                        b.rect.y = mp.y - BLOCK_HEIGHT/2;
                        workspace.push_back(b);
                        draggingIdx = (int)workspace.size()-1;
                        dragOffset  = {mp.x - b.rect.x, mp.y - b.rect.y};
                        break;
                    }
                }
                // drag از workspace
                if (draggingIdx == -1) {
                    for (int i = (int)workspace.size()-1; i >= 0; i--) {
                        if (SDL_PointInRect(&mp, &workspace[i].rect)) {
                            draggingIdx = i;
                            dragOffset  = {mp.x - workspace[i].rect.x,
                                           mp.y - workspace[i].rect.y};
                            break;
                        }
                    }
                }
            }

            if (ev.type == SDL_MOUSEMOTION && draggingIdx >= 0) {
                workspace[draggingIdx].rect.x = ev.motion.x - dragOffset.x;
                workspace[draggingIdx].rect.y = ev.motion.y - dragOffset.y;
            }

            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                if (draggingIdx >= 0) {
                    SDL_Point mp{ev.button.x, ev.button.y};
                    if (mp.x < PALETTE_W)
                        workspace.erase(workspace.begin() + draggingIdx);
                    if (editingIdx >= draggingIdx && editingIdx > 0)
                        editingIdx--;
                    draggingIdx = -1;
                }
            }

            // ── Scroll wheel (تغییر سریع مقدار) ─────────────────────────
            if (ev.type == SDL_MOUSEWHEEL && editingIdx == -1) {
                SDL_Point mp;
                SDL_GetMouseState(&mp.x, &mp.y);
                for (auto& blk : workspace) {
                    if ((blk.type == MOVE_X || blk.type == MOVE_Y) &&
                        SDL_PointInRect(&mp, &blk.rect)) {
                        blk.steps += (ev.wheel.y > 0) ? 5 : -5;
                    }
                }
            }
        }

        UpdateScript();
        Render(r);
    }

    SDL_StopTextInput();
    if (spriteTexture) SDL_DestroyTexture(spriteTexture);
    if (font) TTF_CloseFont(font);
    IMG_Quit(); TTF_Quit();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
