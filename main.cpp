#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <iostream>

// ─── Layout (سایزها برای خروج از حالت فول اسکرین کوچک شدند) ───────────────────
static const int WINDOW_W   = 1150;
static const int WINDOW_H   = 720;
static const int CAT_W      = 260; 
static const int STAGE_W    = 400;
static const int STAGE_H    = 400;
static const int SCRIPTS_X  = CAT_W;
static const int SCRIPTS_W  = WINDOW_W - CAT_W - STAGE_W;
static const int STAGE_X    = CAT_W + SCRIPTS_W;
static const int STAGE_Y    = 20;
static const int BLOCK_W    = 190;
static const int BLOCK_H    = 40;
static const int BLOCK_GAP  = 8;

// ─── Colors ───────────────────────────────────────────────────────────────────
static const SDL_Color COL_MOTION   = {74,  144, 226, 255};
static const SDL_Color COL_LOOKS    = {153, 102, 255, 255};
static const SDL_Color COL_EVENTS   = {255, 171, 25,  255};

// ─── Enums ────────────────────────────────────────────────────────────────────
enum BlockCategory { BCAT_EVENT, BCAT_MOTION, BCAT_LOOKS };
enum BlockType { EVENT_FLAG, CHANGE_X, CHANGE_Y, SET_X, SET_Y, LOOKS_SHOW, LOOKS_HIDE };

// ─── Structs ──────────────────────────────────────────────────────────────────
struct Block {
    SDL_Rect      rect;
    BlockCategory category;
    BlockType     type;
    SDL_Color     color;
    int           steps;
    bool          isHat;
};

struct PaletteHeader {
    std::string name;
    int yPos;
};

// ─── Globals ──────────────────────────────────────────────────────────────────
SDL_Texture* spriteTexture = nullptr;
TTF_Font*    font          = nullptr;
TTF_Font*    fontSmall     = nullptr;

float spriteX = STAGE_W / 2.0f;
float spriteY = STAGE_H / 2.0f;
bool  spriteVisible = true;

std::vector<Block> palette;
std::vector<PaletteHeader> catHeaders;
std::vector<Block> workspace;

// Drag
bool  dragging        = false;
Block dragBlock       = {};
int   dragOffX        = 0, dragOffY = 0;
bool  dragFromPalette = false;
int   dragWorkspaceIdx = -1;

// Edit
bool  editingValue = false;
int   editingIdx   = -1;
std::string inputBuffer;

// Script
bool   scriptRunning = false;
int    scriptStep    = 0;
Uint32 lastStepTime  = 0;
static const int STEP_DELAY = 400;

// ─── Draw Helpers ─────────────────────────────────────────────────────────────
static void FillCircle(SDL_Renderer* r, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)std::sqrt((double)(radius*radius - dy*dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void DrawRoundRect(SDL_Renderer* r, SDL_Rect rect, SDL_Color col, int radius = 6) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_Rect body = {rect.x + radius, rect.y, rect.w - 2*radius, rect.h};
    SDL_RenderFillRect(r, &body);
    SDL_Rect bodyV = {rect.x, rect.y + radius, rect.w, rect.h - 2*radius};
    SDL_RenderFillRect(r, &bodyV);
    FillCircle(r, rect.x + radius,           rect.y + radius,           radius);
    FillCircle(r, rect.x + rect.w - radius,  rect.y + radius,           radius);
    FillCircle(r, rect.x + radius,           rect.y + rect.h - radius,  radius);
    FillCircle(r, rect.x + rect.w - radius,  rect.y + rect.h - radius,  radius);
}

static void DrawText(SDL_Renderer* r, TTF_Font* f, const char* text, int x, int y, SDL_Color col) {
    if (!f) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, text, col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst{x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);
    if (!tex) return;
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

static int TextW(TTF_Font* f, const char* text) {
    int w = 0, h = 0;
    if (f) TTF_SizeUTF8(f, text, &w, &h);
    return w;
}

static int TextH(TTF_Font* f) {
    return f ? TTF_FontHeight(f) : 14;
}

static SDL_Rect DrawValuePill(SDL_Renderer* r, int rx, int ry, int rw, int rh,
                               int value, bool editing, const std::string& buf) {
    const int PW = 46, PH = 22;
    int px = rx + rw - PW - 8;
    int py = ry + (rh - PH) / 2;
    SDL_SetRenderDrawColor(r, 255, 255, 255, 240);
    DrawRoundRect(r, {px, py, PW, PH}, {255,255,255,240}, 10);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 60);
    SDL_Rect pillBorder{px, py, PW, PH};
    SDL_RenderDrawRect(r, &pillBorder);

    std::string display = editing ? buf + "|" : std::to_string(value);
    SDL_Color tc{30,30,30,255};
    int tw = TextW(fontSmall, display.c_str());
    DrawText(r, fontSmall, display.c_str(), px + (PW - tw)/2, py + 3, tc);
    return {px, py, PW, PH};
}

static void DrawHatNotch(SDL_Renderer* r, SDL_Rect br, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    SDL_Rect bump{br.x + 16, br.y - 12, 50, 16};
    DrawRoundRect(r, bump, col, 6);
}

static void DrawBlock(SDL_Renderer* r, const Block& b, bool highlight,
                       bool isEditing, const std::string& buf) {
    SDL_Color c = b.color;
    if (highlight) {
        c.r = (Uint8)std::min(255, (int)c.r + 50);
        c.g = (Uint8)std::min(255, (int)c.g + 50);
        c.b = (Uint8)std::min(255, (int)c.b + 50);
    }
    if (b.isHat) DrawHatNotch(r, b.rect, c);
    DrawRoundRect(r, b.rect, c, 6);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 50);
    SDL_Rect shadow{b.rect.x+2, b.rect.y+2, b.rect.w, b.rect.h};
    SDL_RenderDrawRect(r, &shadow);

    SDL_Color textCol{255, 255, 255, 255};
    const char* label = "";
    switch (b.type) {
        case EVENT_FLAG: label = "When Flag Clicked"; break;
        case CHANGE_X:   label = "Change X by";       break;
        case CHANGE_Y:   label = "Change Y by";       break;
        case SET_X:      label = "Set X to";          break;
        case SET_Y:      label = "Set Y to";          break;
        case LOOKS_SHOW: label = "Show";              break;
        case LOOKS_HIDE: label = "Hide";              break;
    }
    int th = TextH(font);
    int lx = b.rect.x + 12;
    int ly = b.rect.y + (b.rect.h - th) / 2;
    DrawText(r, font, label, lx, ly, textCol);

    if (b.type == CHANGE_X || b.type == CHANGE_Y || b.type == SET_X || b.type == SET_Y) {
        DrawValuePill(r, b.rect.x, b.rect.y, b.rect.w, b.rect.h, b.steps, isEditing, buf);
    }
}

// ─── Engine (All blocks visible at once) ──────────────────────────────────────
void BuildPalette() {
    palette.clear();
    catHeaders.clear();
    
    int x = 25;
    int y = 20;

    auto addHeader = [&](std::string name) {
        catHeaders.push_back({name, y});
        y += 35; 
    };

    auto mk = [&](BlockType t, BlockCategory bc, SDL_Color col, bool hat, int defaultVal) {
        Block b;
        b.rect     = {x, y, BLOCK_W, hat ? BLOCK_H + 12 : BLOCK_H};
        b.category = bc;
        b.type     = t;
        b.color    = col;
        b.steps    = defaultVal;
        b.isHat    = hat;
        if (hat) b.rect.y += 14;
        palette.push_back(b);
        y += b.rect.h + BLOCK_GAP + (hat ? 14 : 0);
    };

    // 1. Events
    addHeader("Events");
    mk(EVENT_FLAG, BCAT_EVENT, COL_EVENTS, true, 0);
    y += 15;

    // 2. Motion
    addHeader("Motion");
    mk(CHANGE_X, BCAT_MOTION, COL_MOTION, false, 10);
    mk(CHANGE_Y, BCAT_MOTION, COL_MOTION, false, 10);
    mk(SET_X,    BCAT_MOTION, COL_MOTION, false, 0);
    mk(SET_Y,    BCAT_MOTION, COL_MOTION, false, 0);
    y += 15;

    // 3. Looks
    addHeader("Looks");
    mk(LOOKS_SHOW, BCAT_LOOKS, COL_LOOKS, false, 0);
    mk(LOOKS_HIDE, BCAT_LOOKS, COL_LOOKS, false, 0);
}

void LayoutWorkspace() {
    int yy = 60;
    for (auto& b : workspace) {
        b.rect.x = SCRIPTS_X + 20;
        b.rect.y = yy;
        b.rect.w = BLOCK_W + 20; // Slightly wider in workspace
        b.rect.h = b.isHat ? BLOCK_H + 12 : BLOCK_H;
        if (b.isHat) { b.rect.y += 14; yy += 14; }
        yy += b.rect.h + BLOCK_GAP;
    }
}

void StartScript() {
    scriptRunning = true;
    scriptStep    = 0;
    lastStepTime  = SDL_GetTicks();
    if (!workspace.empty() && workspace[0].type == EVENT_FLAG)
        scriptStep = 1;
}

void UpdateScript() {
    if (!scriptRunning) return;
    if (scriptStep >= (int)workspace.size()) { scriptRunning = false; return; }
    Uint32 now = SDL_GetTicks();
    if (now - lastStepTime < (Uint32)STEP_DELAY) return;
    lastStepTime = now;

    Block& b = workspace[scriptStep];
    switch (b.type) {
        case CHANGE_X:   spriteX += b.steps; break;
        case CHANGE_Y:   spriteY -= b.steps; break; 
        case SET_X:      spriteX = (STAGE_W / 2.0f) + b.steps; break;
        case SET_Y:      spriteY = (STAGE_H / 2.0f) - b.steps; break;
        case LOOKS_SHOW: spriteVisible = true;  break;
        case LOOKS_HIDE: spriteVisible = false; break;
        default: break;
    }
    spriteX = std::max(30.0f, std::min((float)STAGE_W - 30, spriteX));
    spriteY = std::max(30.0f, std::min((float)STAGE_H - 30, spriteY));
    scriptStep++;
    if (scriptStep >= (int)workspace.size()) scriptRunning = false;
}

// ─── Panels ───────────────────────────────────────────────────────────────────
void DrawStage(SDL_Renderer* r) {
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_Rect stageRect{STAGE_X, STAGE_Y, STAGE_W, STAGE_H};
    SDL_RenderFillRect(r, &stageRect);

    SDL_SetRenderDrawColor(r, 220, 220, 230, 255);
    for (int gx = 40; gx < STAGE_W; gx += 40)
        for (int gy = 40; gy < STAGE_H; gy += 40)
            SDL_RenderDrawPoint(r, STAGE_X + gx, STAGE_Y + gy);

    SDL_SetRenderDrawColor(r, 180, 180, 200, 255);
    SDL_RenderDrawRect(r, &stageRect);

    if (spriteVisible) {
        int sw = 70, sh = 70;
        int sx = STAGE_X + (int)spriteX - sw/2;
        int sy = STAGE_Y + (int)spriteY - sh/2;
        SDL_Rect dst{sx, sy, sw, sh};
        if (spriteTexture) {
            SDL_RenderCopy(r, spriteTexture, nullptr, &dst);
        } else {
            int cx = STAGE_X + (int)spriteX;
            int cy = STAGE_Y + (int)spriteY;
            SDL_SetRenderDrawColor(r, 255, 140, 60, 255);
            FillCircle(r, cx, cy, 28);
            SDL_SetRenderDrawColor(r, 255, 120, 40, 255);
            for (int i = 0; i < 3; i++) {
                SDL_RenderDrawLine(r, cx - 18, cy - 22 + i, cx - 10, cy - 30 + i);
                SDL_RenderDrawLine(r, cx + 18, cy - 22 + i, cx + 10, cy - 30 + i);
            }
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            FillCircle(r, cx - 10, cy - 8, 6);
            FillCircle(r, cx + 10, cy - 8, 6);
            SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
            FillCircle(r, cx - 9, cy - 8, 3);
            FillCircle(r, cx + 11, cy - 8, 3);
            SDL_SetRenderDrawColor(r, 255, 100, 130, 255);
            FillCircle(r, cx, cy + 2, 3);
            SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
            SDL_RenderDrawLine(r, cx - 10, cy + 12, cx, cy + 8);
            SDL_RenderDrawLine(r, cx + 10, cy + 12, cx, cy + 8);
        }
    }

    SDL_SetRenderDrawColor(r, 248, 248, 252, 255);
    SDL_Rect infoBar{STAGE_X, STAGE_Y + STAGE_H + 5, STAGE_W, 35};
    SDL_RenderFillRect(r, &infoBar);
    SDL_SetRenderDrawColor(r, 200, 200, 220, 255);
    SDL_RenderDrawRect(r, &infoBar);

    float scratchX = spriteX - (STAGE_W / 2.0f);
    float scratchY = (STAGE_H / 2.0f) - spriteY;
    
    char info[100];
    SDL_snprintf(info, sizeof(info), "X: %.0f   Y: %.0f   %s", 
                 scratchX, scratchY, spriteVisible ? "Visible" : "Hidden");
    DrawText(r, fontSmall, info, STAGE_X + 10, STAGE_Y + STAGE_H + 12, {80, 80, 100, 255});

    SDL_Rect goBtn{STAGE_X + 10, STAGE_Y + STAGE_H + 50, 90, 36};
    DrawRoundRect(r, goBtn, {0, 200, 80, 255}, 6);
    DrawText(r, font, "GO", goBtn.x + 32, goBtn.y + 10, {255, 255, 255, 255});
    SDL_Rect stopBtn{STAGE_X + 110, STAGE_Y + STAGE_H + 50, 90, 36};
    DrawRoundRect(r, stopBtn, {220, 50, 50, 255}, 6);
    DrawText(r, font, "STOP", stopBtn.x + 24, stopBtn.y + 10, {255, 255, 255, 255});
}

void DrawSpritePanel(SDL_Renderer* r) {
    int py = STAGE_Y + STAGE_H + 95;
    SDL_SetRenderDrawColor(r, 245, 245, 252, 255);
    SDL_Rect panel{STAGE_X, py, STAGE_W, WINDOW_H - py};
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 200, 200, 215, 255);
    SDL_RenderDrawLine(r, STAGE_X, py, STAGE_X + STAGE_W, py);
    DrawText(r, font, "Sprite", STAGE_X + 15, py + 10, {80, 80, 100, 255});

    SDL_SetRenderDrawColor(r, 200, 220, 255, 255);
    SDL_Rect thumb{STAGE_X + 15, py + 40, 70, 60};
    DrawRoundRect(r, thumb, {200, 220, 255, 255}, 6);
    SDL_SetRenderDrawColor(r, 255, 140, 60, 255);
    FillCircle(r, STAGE_X + 50, py + 70, 18);
    SDL_SetRenderDrawColor(r, 74, 144, 226, 255);
    for (int d = 0; d < 2; d++) {
        SDL_Rect sel{thumb.x - d, thumb.y - d, thumb.w + 2*d, thumb.h + 2*d};
        SDL_RenderDrawRect(r, &sel);
    }
    DrawText(r, fontSmall, "Sprite1", STAGE_X + 18, py + 104, {80, 80, 120, 255});

    SDL_Rect visBox{STAGE_X + 100, py + 48, 18, 18};
    SDL_SetRenderDrawColor(r, spriteVisible ? 80 : 200, 
                              spriteVisible ? 160 : 80, 
                              spriteVisible ? 80 : 80, 255);
    SDL_RenderFillRect(r, &visBox);
    DrawText(r, fontSmall, "Visible", STAGE_X + 122, py + 50, {80, 80, 100, 255});
}

void DrawCategoryPanel(SDL_Renderer* r) {
    SDL_SetRenderDrawColor(r, 35, 35, 50, 255);
    SDL_Rect bg{0, 0, CAT_W, WINDOW_H};
    SDL_RenderFillRect(r, &bg);

    // Draw Section Headers
    for (const auto& header : catHeaders) {
        DrawText(r, font, header.name.c_str(), 20, header.yPos, {200, 200, 220, 255});
        SDL_SetRenderDrawColor(r, 100, 100, 120, 255);
        SDL_RenderDrawLine(r, 20, header.yPos + 22, CAT_W - 30, header.yPos + 22);
    }

    // Draw all blocks in palette
    for (auto& b : palette) DrawBlock(r, b, false, false, "");

    // Border
    SDL_SetRenderDrawColor(r, 80, 80, 100, 200);
    SDL_RenderDrawLine(r, CAT_W - 1, 0, CAT_W - 1, WINDOW_H);
}

void DrawScriptsArea(SDL_Renderer* r) {
    SDL_SetRenderDrawColor(r, 240, 240, 248, 255);
    SDL_Rect bg{SCRIPTS_X, 0, SCRIPTS_W, WINDOW_H};
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawColor(r, 225, 225, 235, 255);
    for (int gx = SCRIPTS_X + 20; gx < SCRIPTS_X + SCRIPTS_W; gx += 20)
        for (int gy = 20; gy < WINDOW_H; gy += 20)
            SDL_RenderDrawPoint(r, gx, gy);

    SDL_SetRenderDrawColor(r, 220, 220, 235, 255);
    SDL_Rect header{SCRIPTS_X, 0, SCRIPTS_W, 40};
    SDL_RenderFillRect(r, &header);
    SDL_SetRenderDrawColor(r, 200, 200, 218, 255);
    SDL_RenderDrawLine(r, SCRIPTS_X, 40, SCRIPTS_X + SCRIPTS_W, 40);
    DrawText(r, font, "Scripts Workspace", SCRIPTS_X + 15, 12, {80, 80, 110, 255});
    
    for (int i = 0; i < (int)workspace.size(); i++) {
        bool hi = scriptRunning && (i == scriptStep);
        bool ed = editingValue && (editingIdx == i);
        DrawBlock(r, workspace[i], hi, ed, ed ? inputBuffer : "");
    }
    
    if (workspace.empty()) {
        SDL_Color hint{160, 160, 185, 255};
        DrawText(r, fontSmall, "Drag blocks here to build your script", 
                 SCRIPTS_X + 30, WINDOW_H/2 - 10, hint);
    }
    SDL_SetRenderDrawColor(r, 180, 180, 200, 200);
    SDL_RenderDrawLine(r, SCRIPTS_X + SCRIPTS_W - 1, 0, SCRIPTS_X + SCRIPTS_W - 1, WINDOW_H);
}

void Render(SDL_Renderer* r) {
    SDL_SetRenderDrawColor(r, 200, 200, 215, 255);
    SDL_RenderClear(r);
    DrawCategoryPanel(r);    
    DrawScriptsArea(r);      
    DrawStage(r);            
    DrawSpritePanel(r);      
    if (dragging) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_Color c = dragBlock.color;
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 140);
        DrawRoundRect(r, dragBlock.rect, {c.r, c.g, c.b, 140}, 6);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        DrawBlock(r, dragBlock, false, false, "");
    }
    SDL_RenderPresent(r);
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    TTF_Init();

    SDL_Window* window = SDL_CreateWindow("Scratch Clone - SDL2",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/calibrib.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Arial.ttf",
    };
    for (const char* path : fontPaths) {
        if (!font)      font      = TTF_OpenFont(path, 14);
        if (!fontSmall) fontSmall = TTF_OpenFont(path, 12);
        if (font && fontSmall) break;
    }

    spriteTexture = IMG_LoadTexture(renderer, "sprite.jpg");
    if (!spriteTexture) spriteTexture = IMG_LoadTexture(renderer, "sprite.png");
    BuildPalette();

    bool running = true;
    SDL_Event e;

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }

            if (e.type == SDL_TEXTINPUT && editingValue) {
                for (char ch : std::string(e.text.text)) {
                    if (std::isdigit(ch)) inputBuffer += ch;
                    else if (ch == '-' && inputBuffer.empty()) inputBuffer += ch;
                }
                continue;
            }

            if (e.type == SDL_KEYDOWN && editingValue) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    if (editingIdx >= 0 && editingIdx < (int)workspace.size()) {
                        try {
                            if (inputBuffer == "-" || inputBuffer.empty()) workspace[editingIdx].steps = 0;
                            else workspace[editingIdx].steps = std::stoi(inputBuffer); 
                        } catch (...) { workspace[editingIdx].steps = 0; }
                    }
                    editingValue = false;
                    SDL_StopTextInput();
                } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                    editingValue = false;
                    SDL_StopTextInput();
                } else if (e.key.keysym.sym == SDLK_BACKSPACE && !inputBuffer.empty()) {
                    inputBuffer.pop_back();
                }
                continue;
            }

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x, my = e.button.y;
                SDL_Point mp{mx, my};

                SDL_Rect goBtn{STAGE_X + 10, STAGE_Y + STAGE_H + 50, 90, 36};
                if (SDL_PointInRect(&mp, &goBtn)) {
                    if (!scriptRunning) StartScript();
                    continue;
                }

                SDL_Rect stopBtn{STAGE_X + 110, STAGE_Y + STAGE_H + 50, 90, 36};
                if (SDL_PointInRect(&mp, &stopBtn)) {
                    scriptRunning = false;
                    continue;
                }

                bool clickedBadge = false;
                for (int i = 0; i < (int)workspace.size(); i++) {
                    Block& b = workspace[i];
                    if (b.type != CHANGE_X && b.type != CHANGE_Y && b.type != SET_X && b.type != SET_Y) continue;
                    int px2 = b.rect.x + b.rect.w - 46 - 8;
                    int py2 = b.rect.y + (b.rect.h - 22) / 2;
                    SDL_Rect pill{px2, py2, 46, 22};
                    if (SDL_PointInRect(&mp, &pill)) {
                        editingValue = true;
                        editingIdx   = i;
                        inputBuffer  = std::to_string(b.steps);
                        SDL_StartTextInput();
                        clickedBadge = true;
                        break;
                    }
                }

                if (!clickedBadge && editingValue) {
                    if (editingIdx >= 0 && editingIdx < (int)workspace.size()) {
                        try {
                            if (inputBuffer == "-" || inputBuffer.empty()) workspace[editingIdx].steps = 0;
                            else workspace[editingIdx].steps = std::stoi(inputBuffer); 
                        } catch (...) { workspace[editingIdx].steps = 0; }
                    }
                    editingValue = false;
                    SDL_StopTextInput();
                }

                if (clickedBadge) continue;

                // Drag from left continuous list (Palette)
                for (auto& b : palette) {
                    if (SDL_PointInRect(&mp, &b.rect)) {
                        dragging        = true;
                        dragFromPalette = true;
                        dragBlock       = b;
                        dragOffX        = mx - b.rect.x;
                        dragOffY        = my - b.rect.y;
                        break;
                    }
                }

                // Drag from Workspace
                if (!dragging) {
                    for (int i = (int)workspace.size()-1; i >= 0; i--) {
                        if (SDL_PointInRect(&mp, &workspace[i].rect)) {
                            dragging         = true;
                            dragFromPalette  = false;
                            dragWorkspaceIdx = i;
                            dragBlock        = workspace[i];
                            dragOffX         = mx - workspace[i].rect.x;
                            dragOffY         = my - workspace[i].rect.y;
                            workspace.erase(workspace.begin() + i);
                            LayoutWorkspace();
                            break;
                        }
                    }
                }
            }

            if (e.type == SDL_MOUSEMOTION && dragging) {
                dragBlock.rect.x = e.motion.x - dragOffX;
                dragBlock.rect.y = e.motion.y - dragOffY;
            }

            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && dragging) {
                int mx = e.button.x, my = e.button.y;
                SDL_Point pt{mx, my};
                SDL_Rect wsRect{SCRIPTS_X, 0, SCRIPTS_W, WINDOW_H};

                if (SDL_PointInRect(&pt, &wsRect)) {
                    Block nb = dragBlock;
                    int insertIdx = (int)workspace.size();
                    for (int i = 0; i < (int)workspace.size(); i++) {
                        if (my < workspace[i].rect.y + workspace[i].rect.h / 2) {
                            insertIdx = i;
                            break;
                        }
                    }
                    workspace.insert(workspace.begin() + insertIdx, nb);
                }
                dragging         = false;
                dragWorkspaceIdx = -1;
                LayoutWorkspace();
            }
        }

        UpdateScript();
        Render(renderer);
        SDL_Delay(16);
    }

    if (spriteTexture) SDL_DestroyTexture(spriteTexture);
    if (font)          TTF_CloseFont(font);
    if (fontSmall)     TTF_CloseFont(fontSmall);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
