#include "overworld.h"
#include "../data/sprites.h"
#include <cstdlib>
#include <cstring>
#include <wchar.h>
#include <cstdio>
#ifndef swprintf
#define swprintf _snwprintf
#endif

// ─── 임시 NpcDef 버퍼 (표지판/잠긴 도어 / 풀베기 메시지용) ─────
// 표지판/잠긴문/풀베기는 NPC가 아니지만 dialog 시스템을 재활용하기 위해 NpcDef 형태로 wrap.
static NpcDef _signBuffer;
static NpcDef _lockedDoorBuffer = {
    0, 0,
    { L"문이 잠겨있다...", nullptr, nullptr, nullptr },
    0, 0
};
static NpcDef _cutTreeNeedSkillBuffer = {
    0, 0,
    { L"이 나무는 풀베기로 베어낼 수 있을 것 같다.",
      L"포켓몬에게 풀베기를 가르쳐야 한다.",
      nullptr, nullptr },
    0, 0
};
static NpcDef _cutTreeUsedBuffer = {
    0, 0,
    { L"포켓몬이 풀베기를 사용했다!",
      L"나무가 베어졌다.",
      nullptr, nullptr },
    0, 0
};

// ─── 주인공 GB 도트 스프라이트 매핑 (8 프레임, 4방향 × idle/walk) ─────
//   0 = down idle, 1 = up idle, 2 = left idle, 3 = right idle
//   4 = down walk, 5 = up walk, 6 = left walk, 7 = right walk
static int playerFrameIdx(int dir, int walkFrame) {
    // dir 0=아래, 1=위, 2=왼, 3=오른
    // pokered red.png 16×96 시트 layout (gen_sprites.py 미러링 후):
    //   F0 = DOWN idle, F1 = UP idle, F2 = SIDE idle (LEFT 향함)
    //   F3 = DOWN walk, F4 = UP walk, F5 = SIDE walk (LEFT 향함)
    //   F6 = SIDE idle (RIGHT - mirrored), F7 = SIDE walk (RIGHT - mirrored)
    if (dir == 0) return walkFrame ? 3 : 0;        // 아래
    if (dir == 1) return walkFrame ? 4 : 1;        // 위
    if (dir == 2) return walkFrame ? 5 : 2;        // 왼 (pokered 원본)
    /* dir == 3 */ return walkFrame ? 7 : 6;       // 오른 (mirrored)
}

Overworld::Overworld(Renderer& r, Player& pl)
    : ren_(r), pl_(pl), state_{}
{}

MapDef* Overworld::curMap() const {
    return getMap(state_.mapId);
}

void Overworld::init() {
    state_ = {};
    state_.mapId = pl_.mapId;
    state_.px    = pl_.x;
    state_.py    = pl_.y;
    state_.dir   = pl_.dir;
    state_.pendingEvent = OwEvent::NONE;
    if (pl_.justWokeUp) state_.wakeStep = 1;
    useOverrideLines_ = false;
    memset(overrideLines_, 0, sizeof(overrideLines_));
}

bool Overworld::isNpcHidden(const NpcDef& npc) const {
    if (npc.tag == NPC_TAG_BALL_BULBASAUR  && pl_.starterIdx == 0) return true;
    if (npc.tag == NPC_TAG_BALL_CHARMANDER && pl_.starterIdx == 1) return true;
    if (npc.tag == NPC_TAG_BALL_SQUIRTLE   && pl_.starterIdx == 2) return true;
    if (npc.tag == NPC_TAG_BLUE_LAB && pl_.rivalLabTalked)         return true;
    return false;
}

void Overworld::startNpcDialog(const NpcDef* npc) {
    state_.dialog.active  = true;
    state_.dialog.npc     = npc;
    state_.dialog.lineIdx = 0;
    useOverrideLines_     = false;
    memset(overrideLines_, 0, sizeof(overrideLines_));

    if (npc->tag == NPC_TAG_OAK_LAB && pl_.partySize > 0) {
        useOverrideLines_ = true;
        if (pl_.gotParcel && !pl_.deliveredParcel) {
            overrideLines_[0] = L"오박사: 이게 소포구나! 정말 고마워.";
            overrideLines_[1] = L"오박사: 포켓몬 연구에 꼭 필요한 것이었단다.";
            overrideLines_[2] = L"오박사: [소포를 전달했다!]";
            overrideLines_[3] = nullptr;
        } else if (pl_.deliveredParcel) {
            overrideLines_[0] = L"오박사: 포켓몬 연구에 전념하고 있단다.";
            overrideLines_[1] = L"오박사: 힘껏 모험해!";
            overrideLines_[2] = nullptr;
        } else {
            overrideLines_[0] = L"오박사: 포켓몬과 함께 좋은 여행이 되길 바란다!";
            overrideLines_[1] = nullptr;
        }
    }
    if (npc->tag == NPC_TAG_BLUE_LAB && pl_.beatenRival1 && !pl_.rivalLabTalked) {
        useOverrideLines_ = true;
        overrideLines_[0] = L"블루: 으윽... 운이 좋았던 거야.";
        overrideLines_[1] = L"블루: 다음엔 꼭 이길 거야!";
        overrideLines_[2] = nullptr;
    }
    if (npc->tag == NPC_TAG_VIRIDIAN_CLERK && pl_.gotParcel) {
        useOverrideLines_ = true;
        overrideLines_[0] = L"점원: 어서오세요!";
        overrideLines_[1] = L"점원: 필요한 게 있으면 말씀해 주세요.";
        overrideLines_[2] = nullptr;
    }
}

// ─── 깨어나는 대사 ────────────────────────────────────────────
static const wchar_t* WAKE_LINES[] = {
    L"...... 쿨 ...... 쿨 ......",
    L"...... 응? ......",
    L"아침이다! 정신을 차리고 일어나야지!",
    L"오박사님 연구소에 한번 가봐야겠다!",
};
static const int WAKE_LINE_COUNT = 4;

// ─── 걷기 애니메이션 ──────────────────────────────────────────
// 한 타일 이동에 STEP_FRAMES 프레임 (16fps 기준 약 250ms).
// 가로 4칸, 세로 2칸을 STEP_FRAMES에 걸쳐 선형 보간.
static const int STEP_FRAMES = 4;

OwEvent Overworld::popEvent() {
    OwEvent ev = state_.pendingEvent;
    state_.pendingEvent = OwEvent::NONE;
    return ev;
}

// ─── OW cutscene 시작 ────────────────────────────────────────
void Overworld::startOakIntercept() {
    // 오박사가 player 뒤(아래)에서 등장 → player 옆까지 따라옴 → 대사 → 페이드 → 연구소
    // (원작도 player 풀숲 진입 시도 시 OAK가 따라와 막음 — 뒷모습 등장)
    state_.cutscene = {};
    state_.cutscene.type     = CutsceneType::OAK_INTERCEPT;
    state_.cutscene.step     = 0;
    state_.cutscene.ax       = state_.px;
    state_.cutscene.ay       = state_.py + 5;   // 뒤(아래)에서
    state_.cutscene.targetX  = state_.px;
    state_.cutscene.targetY  = state_.py + 1;   // player 아래 1칸
    state_.cutscene.adir     = 1;  // 위 (player 향함, 뒷모습)
    state_.cutscene.spriteId = NPC_SPR_OAK;
    state_.cutscene.walkTimer = 8;
    state_.cutscene.dialogIdx = 0;
}

void Overworld::startRivalBlock() {
    // 블루가 player 아래쪽 출구 부근에서 등장 → 옆에 멈춤 → 대사 → 배틀
    state_.cutscene = {};
    state_.cutscene.type     = CutsceneType::RIVAL_BLOCK;
    state_.cutscene.step     = 0;
    state_.cutscene.ax       = state_.px;
    state_.cutscene.ay       = state_.py + 4;   // 출구 쪽에서 등장
    state_.cutscene.targetX  = state_.px;
    state_.cutscene.targetY  = state_.py + 1;
    state_.cutscene.adir     = 1;  // 위 (player 마주봄)
    state_.cutscene.spriteId = NPC_SPR_BLUE;
    state_.cutscene.walkTimer = 8;
    state_.cutscene.dialogIdx = 0;
}

// ─── cutscene 단계 진행 ──────────────────────────────────────
static const wchar_t* OAK_CUT_LINES[] = {
    L"오박사: 잠깐! 거긴 위험해!",
    L"오박사: 야생 포켓몬은 풀숲 어디든 있단다.",
    L"오박사: 자기 포켓몬 없이는 못 가!",
    L"오박사: 자, 같이 내 연구소로 가자.",
    nullptr
};
static const wchar_t* RIVAL_CUT_LINES[] = {
    L"블루: 잠깐! 거기 서!",
    L"블루: 우리 포켓몬 한 번 비교해보자!",
    L"블루: 내가 이길 거야!",
    L"블루: 한 판 붙어보자!",
    nullptr
};

bool Overworld::cutsceneActive() const {
    return state_.cutscene.type != CutsceneType::NONE;
}

void Overworld::updateCutscene(Key key) {
    OwCutscene& cs = state_.cutscene;

    // ── 페이드 아웃 (오박사 cutscene 끝) ──
    if (cs.fadingOut) {
        cs.fadeFrame--;
        if (cs.fadeFrame <= 0) {
            // OAK_INTERCEPT 끝 → 연구소 OW 워프 emit
            cs.type = CutsceneType::NONE;
            state_.pendingEvent = OwEvent::CUTSCENE_END_OAK;
        }
        return;
    }

    // ── walking ── (자동 걷기, target까지)
    if (cs.step == 0) {
        cs.walkTimer--;
        if (cs.walkTimer <= 0) {
            // 한 칸 이동
            int dx = (cs.targetX > cs.ax) - (cs.targetX < cs.ax);
            int dy = (cs.targetY > cs.ay) - (cs.targetY < cs.ay);
            if (dx || dy) {
                cs.ax += dx; cs.ay += dy;
                cs.adir = (dy > 0) ? 0 : (dy < 0) ? 1 : (dx < 0) ? 2 : 3;
                cs.walkTimer = 8;
            } else {
                // 도착 — 대사 시작
                cs.step = 1;
            }
        }
        return;
    }

    // ── dialog ── (key 입력으로 대사 진행)
    if (cs.step == 1) {
        if (key == Key::A || key == Key::B) {
            cs.dialogIdx++;
            const wchar_t** lines = (cs.type == CutsceneType::OAK_INTERCEPT)
                                  ? OAK_CUT_LINES : RIVAL_CUT_LINES;
            if (!lines[cs.dialogIdx]) {
                // 대사 끝
                if (cs.type == CutsceneType::OAK_INTERCEPT) {
                    cs.fadingOut = true;
                    cs.fadeFrame = 18;
                } else {
                    // RIVAL_BLOCK: 즉시 배틀 emit
                    cs.type = CutsceneType::NONE;
                    state_.pendingEvent = OwEvent::CUTSCENE_END_RIVAL;
                }
            }
        }
        return;
    }
}

void Overworld::onReturnFromBattle(bool won) {
    // 좌표 동기화만 수행 — 팔레트 워프(전멸 처리)는 game.cpp의 LOSE 분기 단독 책임.
    // 도망(ESCAPE)/잡몹 승리 모두 won=false/true 와 무관하게 현재 위치 유지.
    (void)won;
    pl_.mapId = state_.mapId;
    pl_.x = state_.px; pl_.y = state_.py;
}

void Overworld::onReturnFromCenter() {
    pl_.mapId = state_.mapId;
    pl_.x = state_.px; pl_.y = state_.py;
}

// ─── 타일 렌더 (1char 기반 버퍼 fallback - 더 이상 직접 호출 안 함) ─────
void Overworld::drawTile(int sx, int sy, char tile) {
    char ch;
    std::string col = Color::BG_BLACK;
    switch (tile) {
        case '#': ch = '#'; col += Color::BRIGHT_BLACK; break;
        case 'T': ch = 'Y'; col += Color::GREEN;        break;
        case '~': ch = '~'; col += Color::BRIGHT_BLUE;  break;
        case ',': ch = ','; col += Color::GREEN;         break;
        case ';': ch = '"'; col += Color::BRIGHT_GREEN;  break;
        case '.': ch = '.'; col += Color::WHITE;         break;
        case 'D': case 'L': case 'C': case 'M':
        case 'G': ch = '+'; col += Color::BRIGHT_YELLOW; break;
        case 's': ch = 's'; col += Color::BRIGHT_CYAN;   break;
        case 'N': ch = '!'; col += Color::BRIGHT_WHITE;  break;
        case 'H': ch = '['; col += Color::BRIGHT_BLACK;  break;
        case 'B': ch = 'B'; col += Color::BRIGHT_MAGENTA;break;
        default:  ch = ' '; col += Color::BLACK;         break;
    }
    ren_.setCell(sx, sy, ch, col);
}

// ─── 이동 처리 ───────────────────────────────────────────────
void Overworld::tryMove(int dx, int dy) {
    // 이미 이동 애니메이션 중이면 입력 무시 (한 번에 한 타일씩)
    if (state_.moving > 0) return;

    MapDef* m = curMap();
    if (!m) return;

    // 방향 설정 (벽 부딪힘이라도 방향은 돌림)
    if (dy > 0) state_.dir = 0;
    else if (dy < 0) state_.dir = 1;
    else if (dx < 0) state_.dir = 2;
    else if (dx > 0) state_.dir = 3;

    int nx = state_.px + dx;
    int ny = state_.py + dy;

    // 스타터 없는 상태에서 팔레트시티 북쪽 진입 → 오박사 저지
    if (ny < 0 && state_.mapId == MAP_PALLET && pl_.partySize == 0) {
        state_.pendingEvent = OwEvent::OAK_INTERCEPT;
        return;
    }

    // 맵 경계 이탈 → 인접 맵 이동 (즉시 워프, 애니메이션 없음)
    // northEntryX/southEntryX = pokered connection offset (블록×2 스텝)
    // dest_x = source_x + offset (너비가 다른 맵 간 정확한 위치 보존)
    if (ny < 0 && m->northMap >= 0) {
        MapDef* nm = getMap(m->northMap);
        if (nm) {
            state_.mapId = m->northMap;
            state_.px = nx + m->northEntryX;
            if (state_.px < 0) state_.px = 0;
            if (state_.px >= nm->mapW) state_.px = nm->mapW - 1;
            state_.py = nm->mapH - 1;
            pl_.mapId = state_.mapId;
            pl_.x = state_.px; pl_.y = state_.py;
        }
        return;
    }
    if (ny >= m->mapH && m->southMap >= 0) {
        MapDef* sm = getMap(m->southMap);
        if (sm) {
            state_.mapId = m->southMap;
            state_.px = nx + m->southEntryX;
            if (state_.px < 0) state_.px = 0;
            if (state_.px >= sm->mapW) state_.px = sm->mapW - 1;
            state_.py = 0;
            pl_.mapId = state_.mapId;
            pl_.x = state_.px; pl_.y = state_.py;
        }
        return;
    }

    // 좌우 경계 + 인접 맵 없을 때의 위/아래 경계 — 맵 밖(검정 영역)으로 못 나감
    if (nx < 0 || nx >= m->mapW) return;
    if (ny < 0 || ny >= m->mapH) return;

    char t = getTile(m, nx, ny);
    // 풀베기로 베어낸 'T' 나무는 빈 공간으로 처리 (walkable)
    if (t == 'T' && isTreeCut(pl_, state_.mapId, nx, ny)) t = '!';

    // 절벽 (ledge) jump-down: 남쪽 이동(dy>0)일 때 ledge tile 위로 + 한 칸 더 내려감
    // v3 overworld의 ledge atoms (BL=0x36=54, 0x37=55): chars '0', '5', '6', '<'
    //   '0' [44,44,55,55]  grass top + ledge bottom
    //   '5' [57,57,54,55]  path top + ledge bottom (SW variant)
    //   '6' [57,57,55,55]  path top + ledge bottom (SE variant)
    //   '<' [44,44,55,52]  grass top + ledge-stair bottom (계단 좌측, 시각은 계단이지만 BL=55라 점프)
    // ('9' [20,20,20,20] 는 ledge 아님 — 단순 path/dirt 타일)
    // ('?' [44,44,60,60] BL=60=passable → 일반 walkable, ledge 아님)
    bool isLedge = (state_.mapId >= 0 && state_.mapId <= 5 && state_.mapId != 4) &&
                   (t == '0' || t == '5' || t == '6' || t == '<');
    if (isLedge && dy > 0) {
        // 점프: ny+1 까지 이동 (총 2칸)
        int jy = ny + 1;
        if (jy < m->mapH) {
            char jt = getTile(m, nx, jy);
            if (tileWalkable(jt, state_.mapId)) {
                state_.stepDx = dx;
                state_.stepDy = dy;
                state_.px = nx;
                state_.py = jy;
                pl_.x = state_.px;
                pl_.y = state_.py;
                state_.dir = 0;
                state_.moving = 8;  // animation frames
                checkSpecialTiles(state_.px, state_.py);
                checkWarps(state_.px, state_.py);
                return;
            }
        }
    }

    if (!tileWalkable(t, state_.mapId)) {
        // 워프 트리거 char (player가 닿는 순간 워프):
        //   D=문 / L=lab door / C=player house 정문 / M=mart / G=gym
        //   p=R2F 사다리 / ? = OakLab 도어 / w=R1F→R2F 사다리 / j=R2F→R1F 사다리
        if (t == 'D' || t == 'L' || t == 'C' || t == 'M' || t == 'G' ||
            t == 'p' || t == '?' || t == 'w' || t == 'j') {
            checkWarps(nx, ny);
        }
        return;
    }

    // NPC/트레이너 충돌 — 같은 좌표에 NPC가 있으면 차단
    for (int i = 0; i < m->numNpcs; i++) {
        if (m->npcs[i].x == nx && m->npcs[i].y == ny) return;
    }
    for (int i = 0; i < m->numTrainers; i++) {
        if (!m->trainers[i].defeated &&
            m->trainers[i].x == nx && m->trainers[i].y == ny) return;
    }

    // 걷기 애니메이션 시작: 논리 좌표는 즉시 갱신, 시각만 보간
    state_.stepDx  = dx;
    state_.stepDy  = dy;
    state_.px      = nx;
    state_.py      = ny;
    pl_.x          = nx;
    pl_.y          = ny;
    state_.moving  = STEP_FRAMES;
    state_.walkStep++;

    // 워프/특수 타일 체크는 애니메이션 종료 후로 미룸 (update에서 처리)
}

void Overworld::checkWarps(int x, int y) {
    MapDef* m = curMap();
    if (!m) return;
    char tile = getTile(m, x, y);

    // 'C' / 'M' 워프는 아래 WarpDef 로직으로 처리됨
    // (PC: MAP_VIRIDIAN_PC/MAP_PEWTER_PC, Mart: MAP_VIRIDIAN_MART/MAP_PEWTER_MART)
    // 소포 이벤트는 상록 마트 점원 NPC 와 대화로 처리 (trigger=3)

    for (int i = 0; i < m->numWarps; i++) {
        const WarpDef& w = m->warps[i];
        if (w.srcX == x && w.srcY == y) {
            MapDef* dest = getMap(w.destMap);
            if (!dest) {
                // destMap == -1 (미구현 건물) → 잠긴 문 메시지
                state_.dialog.active = true;
                state_.dialog.npc = &_lockedDoorBuffer;
                state_.dialog.lineIdx = 0;
                return;
            }
            // 페이드 트랜지션 시작 — 즉시 워프하지 않고 mid-fade에 실행
            int dx = (w.destX >= 0 && w.destY >= 0) ? w.destX : dest->southEntryX;
            int dy = (w.destX >= 0 && w.destY >= 0) ? w.destY : dest->southEntryY;
            state_.warpFlashFrames = 12;
            state_.pendingWarpMap  = w.destMap;
            state_.pendingWarpX    = dx;
            state_.pendingWarpY    = dy;
            // 이동 중 애니메이션 강제 종료 (오버랩 방지)
            state_.moving = 0;
            return;
        }
    }
}

void Overworld::checkSpecialTiles(int x, int y) {
    MapDef* m = curMap();
    if (!m) return;

    // 긴 풀 인카운터
    if (tileIsEncounter(getTile(m, x, y)) && m->numEncounters > 0) {
        if (rand() % 100 < 15) { // 15% 확률
            triggerEncounter();
        }
    }

    // NPC 상호작용 체크는 update에서 A키로 처리
}

void Overworld::triggerEncounter() {
    MapDef* m = curMap();
    if (!m || m->numEncounters == 0) return;

    // 가중치 기반 랜덤 선택
    int total = 0;
    for (int i = 0; i < m->numEncounters; i++) total += m->encounters[i].weight;
    int r = rand() % total;
    int acc = 0;
    for (int i = 0; i < m->numEncounters; i++) {
        acc += m->encounters[i].weight;
        if (r < acc) {
            const EncounterEntry& e = m->encounters[i];
            state_.wildSpeciesId = e.speciesId;
            state_.wildLevel = e.minLevel + rand() % (e.maxLevel - e.minLevel + 1);
            state_.pendingEvent = OwEvent::WILD_ENCOUNTER;
            return;
        }
    }
}

bool Overworld::checkTrainerSight() {
    return false;   // 시야 자동배틀 비활성화 — A키 상호작용으로 대체
    MapDef* m = curMap();
    if (!m) return false;
    for (int i = 0; i < m->numTrainers; i++) {
        TrainerDef& tr = const_cast<TrainerDef&>(m->trainers[i]);
        if (tr.defeated) continue;
        // 트레이너의 시야 방향 검사
        int dx = state_.px - tr.x;
        int dy = state_.py - tr.y;
        bool inSight = false;
        switch (tr.dir) {
            case 0: inSight = (dx == 0 && dy > 0 && dy <= tr.sightRange); break; // 아래
            case 1: inSight = (dx == 0 && dy < 0 && -dy <= tr.sightRange); break; // 위
            case 2: inSight = (dy == 0 && dx < 0 && -dx <= tr.sightRange); break; // 왼
            case 3: inSight = (dy == 0 && dx > 0 && dx <= tr.sightRange); break;  // 오른
        }
        if (inSight) {
            state_.eventData = i;
            // isBoss=true 트레이너 = 체육관 관장 등 → BOSS_BATTLE
            if (tr.isBoss) {
                state_.pendingEvent = OwEvent::BOSS_BATTLE;
            } else {
                state_.pendingEvent = OwEvent::TRAINER_BATTLE;
            }
            return true;
        }
    }
    return false;
}

// ─── 업데이트 ────────────────────────────────────────────────
void Overworld::update(Key key) {
    state_.frame++;

    // 워프 페이드 진행 — 12 → 0. mid-point (6)에서 실제 맵 전환.
    if (state_.warpFlashFrames > 0) {
        state_.warpFlashFrames--;
        if (state_.warpFlashFrames == 6) {
            // mid-fade — 실제 워프 실행
            state_.mapId = state_.pendingWarpMap;
            state_.px    = state_.pendingWarpX;
            state_.py    = state_.pendingWarpY;
            pl_.mapId    = state_.mapId;
            pl_.x        = state_.px;
            pl_.y        = state_.py;
            // 도착 후 추가 워프 트리거(연쇄 워프) 방지 — 다음 이동 시점에 재계산
        }
        return;  // 페이드 중엔 다른 입력/이동 무시
    }

    // cutscene 모드 — player 입력 차단, 자동 진행
    if (cutsceneActive()) {
        updateCutscene(key);
        return;
    }

    // 걷기 애니메이션 진행 — 도중엔 입력 무시
    if (state_.moving > 0) {
        state_.moving--;
        if (state_.moving == 0) {
            // 애니메이션 종료 시점에 워프/인카운터/시야 체크
            checkWarps(state_.px, state_.py);
            checkSpecialTiles(state_.px, state_.py);
        }
        return;
    }

    // 깨어나는 시퀀스 (인트로 직후 1회만)
    if (state_.wakeStep > 0) {
        if (key == Key::A || key == Key::B) {
            state_.wakeStep++;
            if (state_.wakeStep > WAKE_LINE_COUNT) {
                state_.wakeStep = 0;
                pl_.justWokeUp = false;
            }
        }
        return;
    }

    // NPC 대화 중
    if (state_.dialog.active) {
        if (key == Key::A || key == Key::B) {
            state_.dialog.lineIdx++;
            const NpcDef* npc = state_.dialog.npc;
            const wchar_t* curLine = useOverrideLines_
                ? overrideLines_[state_.dialog.lineIdx]
                : (state_.dialog.lineIdx < 4 ? npc->lines[state_.dialog.lineIdx] : nullptr);
            if (state_.dialog.lineIdx >= 4 || !curLine) {
                state_.dialog.active  = false;
                useOverrideLines_     = false;

                // STARTER_TRIGGER: partySize==0일 때만
                if (npc->trigger == 1 && pl_.partySize == 0) {
                    state_.pendingEvent = OwEvent::STARTER_TRIGGER;
                }
                if (npc->trigger == 2) {
                    state_.pendingEvent = OwEvent::NURSE_HEAL;
                }
                // 소포 수령 (gotParcel)
                if (npc->trigger == 3 && state_.mapId == MAP_VIRIDIAN_MART && !pl_.gotParcel) {
                    pl_.gotParcel = true;
                }
                // 오박사 소포 전달 완료
                if (npc->tag == NPC_TAG_OAK_LAB && pl_.gotParcel && !pl_.deliveredParcel) {
                    pl_.deliveredParcel = true;
                    // 퀘스트 보상 연동 위치 — ITEM_SYSTEM_INTEGRATION.md 참고
                }
                // 블루 배틀 후 대화 완료 → 연구소에서 사라짐
                if (npc->tag == NPC_TAG_BLUE_LAB && pl_.beatenRival1 && !pl_.rivalLabTalked) {
                    pl_.rivalLabTalked = true;
                }
            }
        }
        return;
    }

    // 이동
    if (state_.pendingEvent != OwEvent::NONE) return;

    if (key == Key::UP)    tryMove(0, -1);
    if (key == Key::DOWN)  tryMove(0,  1);
    if (key == Key::LEFT)  tryMove(-1, 0);
    if (key == Key::RIGHT) tryMove(1,  0);

    if (state_.pendingEvent != OwEvent::NONE) return;

    // A키: 앞쪽 NPC/표지판과 상호작용
    if (key == Key::A) {
        int dfx = 0, dfy = 0;
        switch (state_.dir) {
            case 0: dfy =  1; break;  // 아래
            case 1: dfy = -1; break;  // 위
            case 2: dfx = -1; break;  // 왼쪽
            case 3: dfx =  1; break;  // 오른쪽
        }
        int fx = state_.px + dfx, fy = state_.py + dfy;
        MapDef* m = curMap();
        if (m) {
            bool talked = false;
            for (int i = 0; i < m->numNpcs; i++) {
                if (m->npcs[i].x == fx && m->npcs[i].y == fy) {
                    if (isNpcHidden(m->npcs[i])) break;
                    startNpcDialog(&m->npcs[i]);
                    talked = true;
                    break;
                }
            }
            if (!talked && !tileWalkable(getTile(m, fx, fy), state_.mapId)) {
                int fx2 = fx + dfx, fy2 = fy + dfy;
                for (int i = 0; i < m->numNpcs; i++) {
                    if (m->npcs[i].x == fx2 && m->npcs[i].y == fy2) {
                        if (isNpcHidden(m->npcs[i])) break;
                        startNpcDialog(&m->npcs[i]);
                        talked = true;
                        break;
                    }
                }
            }
            // 표지판 체크 — ALL_SIGNS 에서 (mapId, fx, fy) 일치하는 표지판 찾기
            if (!talked) {
                const SignDef* sign = findSign(state_.mapId, fx, fy);
                if (sign) {
                    for (int i = 0; i < 4; i++) _signBuffer.lines[i] = sign->lines[i];
                    _signBuffer.trigger = 0;
                    state_.dialog.active = true;
                    state_.dialog.npc = &_signBuffer;
                    state_.dialog.lineIdx = 0;
                    talked = true;
                }
            }
            // 트레이너 체크 — 앞에 있는 트레이너에게 말 걸면 배틀 시작
            if (!talked) {
                for (int i = 0; i < m->numTrainers; i++) {
                    TrainerDef& tr = const_cast<TrainerDef&>(m->trainers[i]);
                    if (tr.defeated) continue;
                    if (tr.x == fx && tr.y == fy) {
                        state_.eventData = i;
                        if (tr.isBoss)
                            state_.pendingEvent = OwEvent::BOSS_BATTLE;
                        else
                            state_.pendingEvent = OwEvent::TRAINER_BATTLE;
                        talked = true;
                        break;
                    }
                }
            }
            // 풀베기 나무 체크 — 'T' 타일 마주보고 A 누르면 처리
            if (!talked) {
                char ft = getTile(m, fx, fy);
                if (ft == 'T' && !isTreeCut(pl_, state_.mapId, fx, fy)) {
                    if (playerHasCut(pl_)) {
                        addCutTree(pl_, state_.mapId, fx, fy);
                        state_.dialog.active = true;
                        state_.dialog.npc = &_cutTreeUsedBuffer;
                        state_.dialog.lineIdx = 0;
                    } else {
                        state_.dialog.active = true;
                        state_.dialog.npc = &_cutTreeNeedSkillBuffer;
                        state_.dialog.lineIdx = 0;
                    }
                }
            }
            // 'C'/'M' 에 A 누르는 건 사인 보는 용도 (별도 처리 불필요 — 워프 시 자동)
        }
    }

    // 트레이너 시야 체크는 걸음 종료 시점에 처리 (위 moving==0 분기에서)
    // — 이동 시작 직후엔 스킵하여 애니메이션 중 이중 트리거 방지
}

// ─── 렌더 ────────────────────────────────────────────────────
// render(): 배경(검정)만 세팅. 실제 타일 아트는 renderKorean()에서 printRaw로 출력.
void Overworld::render() {
    int W = ren_.width;
    int H = ren_.height;
    int viewH = H - 6;

    // 맵 영역: 검정 배경
    ren_.fillRect(0, 0, W, viewH, ' ', std::string(Color::BG_BLACK) + Color::BLACK);

    // 대화창 박스 (하단 6행)
    int boxY = viewH;
    int boxH = 6;
    ren_.drawBox(0, boxY, W, boxH, std::string(Color::BG_BLACK) + Color::WHITE);
    ren_.fillRect(1, boxY+1, W-2, boxH-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
}

void Overworld::renderKorean() {
    int W = ren_.width;
    int H = ren_.height;
    int viewH = H - 6;
    int boxY = viewH;

    MapDef* m = curMap();

    // 워프 페이드 중이면 검정 화면만 — 맵 렌더 스킵
    if (state_.warpFlashFrames > 0) {
        ren_.fillRect(0, 0, W, viewH, ' ',
            std::string(Color::BG_BLACK) + Color::BLACK);
        // 하단 박스는 유지
        ren_.drawBox(0, boxY, W, 6, std::string(Color::BG_BLACK) + Color::WHITE);
        ren_.fillRect(1, boxY+1, W-2, 4, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        return;
    }

    // ── 실제 타일 아트 렌더링 (pokered 스프라이트 기반) ──────────
    // 타일 1개 = 4chars 너비 × 2행 (TILE_COLS=2 blocks × 2chars each, TILE_ROWS=2)
    // 화면에 보이는 타일 수: tilesX = W/4, tilesY = viewH/2
    {
        int tilesX = W / 16;    // 가로 타일 수 (각 타일=16chars)
        int tilesY = viewH / 8; // 세로 타일 수 (각 타일=8행)
        int camX = state_.px - tilesX / 2;
        int camY = state_.py - tilesY / 2;

        bool indoor = m && isIndoorMap(m->id);

        for (int ty = 0; ty < tilesY; ty++) {
            for (int tx = 0; tx < tilesX; tx++) {
                int mx = camX + tx;
                int my = camY + ty;
                int sx = tx * 16;    // 터미널 X (각 타일 16chars)
                int sy = ty * 8;     // 터미널 Y (각 타일 8행)

                char tile_char = ' ';
                bool isNpc = false, isTr = false;
                int  npcSpriteId = NPC_SPR_MOM;
                int  trSpriteId  = NPC_SPR_GENTLEMAN;
                bool offMap = false;
                int  artMapId = state_.mapId;

                if (m && mx >= 0 && mx < m->mapW && my >= 0 && my < m->mapH) {
                    tile_char = getTile(m, mx, my);
                    // 풀베기로 베어낸 나무는 풀밭으로 렌더
                    if (tile_char == 'T' && isTreeCut(pl_, state_.mapId, mx, my))
                        tile_char = '!';
                    for (int i = 0; i < m->numNpcs; i++)
                        if (m->npcs[i].x == mx && m->npcs[i].y == my) {
                            if (!isNpcHidden(m->npcs[i])) {
                                isNpc = true;
                                npcSpriteId = m->npcs[i].spriteId;
                            }
                            break;
                        }
                    for (int i = 0; i < m->numTrainers; i++)
                        if (!m->trainers[i].defeated &&
                            m->trainers[i].x == mx && m->trainers[i].y == my) {
                            isTr = true;
                            trSpriteId = m->trainers[i].spriteId;
                            break;
                        }
                } else if (m && !indoor && my < 0 && m->northMap >= 0) {
                    // 북쪽 이웃 outdoor 맵 픽업 — pokered 식 seamless scroll
                    MapDef* nm = getMap(m->northMap);
                    if (nm && !isIndoorMap(nm->id)) {
                        int nmx = mx + m->northEntryX;
                        int nmy = nm->mapH + my;
                        if (nmx >= 0 && nmx < nm->mapW && nmy >= 0 && nmy < nm->mapH) {
                            tile_char = getTile(nm, nmx, nmy);
                            artMapId = nm->id;
                        } else {
                            offMap = true;
                        }
                    } else {
                        offMap = true;
                    }
                } else if (m && !indoor && my >= m->mapH && m->southMap >= 0) {
                    // 남쪽 이웃 outdoor 맵
                    MapDef* sm = getMap(m->southMap);
                    if (sm && !isIndoorMap(sm->id)) {
                        int smx = mx + m->southEntryX;
                        int smy = my - m->mapH;
                        if (smx >= 0 && smx < sm->mapW && smy >= 0 && smy < sm->mapH) {
                            tile_char = getTile(sm, smx, smy);
                            artMapId = sm->id;
                        } else {
                            offMap = true;
                        }
                    } else {
                        offMap = true;
                    }
                } else {
                    offMap = true;
                }

                // 실내 맵의 외곽 영역은 그리지 않음 (clear()의 검정 배경 그대로)
                if (offMap && indoor) continue;

                // 실외 맵 off-map (이웃 없거나 범위 밖) → 트리
                if (offMap && !indoor) tile_char = '$';

                // 실내 맵: ' '(빈 칸) → 풀밭 대신 나무 바닥
                if (indoor && tile_char == ' ') tile_char = 'f';

                const TileArt* art = getTileArt(tile_char, artMapId);

                // NPC/트레이너는 바탕 타일 위에 오버레이 (renderKorean에서 처리)
                for (int r = 0; r < TILE_ROWS && sy + r < viewH; r++) {
                    if (art->rows[r]) ren_.printRaw(sx, sy + r, art->rows[r]);
                }

                // NPC/Trainer 오버레이 — spriteId로 lookup (sprites.h getNpcSpritePtr)
                // 16 chars × 8 rows = 1 walkable 타일에 정확히 들어감
                if (isNpc || isTr) {
                    const OwPlayerFrame* spr =
                        getNpcSpritePtr(isNpc ? npcSpriteId : trSpriteId);
                    for (int r = 0; r < 8; r++) {
                        if (spr->rows[r])
                            ren_.printRaw(sx, sy + r, spr->rows[r]);
                    }
                }
            }
        }
    }

    // 맵 이름 (왼쪽 상단)
    if (m && m->nameW)
        ren_.printW(1, 0, m->nameW,
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);

    // ── 주인공 GB 도트 스프라이트 (16 chars × 8 rows = 정확히 1 walkable 타일) ──
    {
        int tilesX = W / 16;
        int tilesY = viewH / 8;
        int camX = state_.px - tilesX / 2;
        int camY = state_.py - tilesY / 2;
        int sx = (state_.px - camX) * 16;
        int sy = (state_.py - camY) * 8;

        // 슬라이드 보간 (16 chars × 8 rows per 타일)
        if (state_.moving > 0) {
            sx -= state_.stepDx * 16 * state_.moving / STEP_FRAMES;
            sy -= state_.stepDy * 8 * state_.moving / STEP_FRAMES;
        }

        int dir = state_.dir;
        int walkFrame = (state_.moving > 0) ? (state_.walkStep & 1) : 0;
        int frameIdx = playerFrameIdx(dir, walkFrame);
        if (frameIdx < 0 || frameIdx >= OW_PLAYER_FRAMES) frameIdx = 0;

        const OwPlayerFrame& frm = OW_PLAYER_RED[frameIdx];

        // 16×8 스프라이트 = 1×1 우리타일. 타일 위치(sx, sy)에 그대로 정렬.
        int spriteX = sx;
        int spriteY = sy;

        for (int r = 0; r < OW_PLAYER_H; r++) {
            int yy = spriteY + r;
            if (yy < 0 || yy >= viewH) continue;
            if (frm.rows[r]) ren_.printRaw(spriteX, yy, frm.rows[r]);
        }
    }

    // ── Cutscene actor + 대사창 + 페이드 ──────────────────────
    if (cutsceneActive()) {
        const OwCutscene& cs = state_.cutscene;

        // 페이드 아웃 (오박사 끝): 검정 화면
        if (cs.fadingOut) {
            ren_.fillRect(0, 0, W, H, ' ',
                std::string(Color::BG_BLACK) + Color::BLACK);
            return;
        }

        // actor sprite — dir에 따라 down/up 선택 (dir=1 위로 걸어갈 때 up idle)
        int tilesX = W / 16;
        int tilesY = viewH / 8;
        int camX = state_.px - tilesX / 2;
        int camY = state_.py - tilesY / 2;
        int sx = (cs.ax - camX) * 16;
        int sy = (cs.ay - camY) * 8;
        const OwPlayerFrame* spr = getNpcSpritePtr(cs.spriteId, cs.adir);
        if (sy >= 0 && sy + 8 <= viewH) {
            for (int r = 0; r < 8; r++)
                if (spr->rows[r])
                    ren_.printRaw(sx, sy + r, spr->rows[r]);
        }

        // 대사창 (step >= 1)
        if (cs.step >= 1) {
            const wchar_t** lines = (cs.type == CutsceneType::OAK_INTERCEPT)
                                   ? OAK_CUT_LINES : RIVAL_CUT_LINES;
            if (lines[cs.dialogIdx]) {
                ren_.printW(2, boxY + 2, lines[cs.dialogIdx],
                    std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
                ren_.printW(2, boxY + 4, L"[ Z: 다음 ]",
                    std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
            }
        } else {
            // walking 단계 — "..." 정도만
            ren_.printW(2, boxY + 2, L"......",
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        }
        return;
    }

    // 깨어나는 시퀀스 대사
    if (state_.wakeStep > 0) {
        int idx = state_.wakeStep - 1;
        if (idx >= 0 && idx < WAKE_LINE_COUNT) {
            ren_.printW(2, boxY + 2, WAKE_LINES[idx],
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        }
        ren_.printW(2, boxY + 4, L"[ Z: 다음 ]",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        return;
    }

    // NPC 대화
    if (state_.dialog.active && state_.dialog.npc) {
        const NpcDef* npc = state_.dialog.npc;
        int li = state_.dialog.lineIdx;
        const wchar_t* line = useOverrideLines_
            ? (li < 4 ? overrideLines_[li] : nullptr)
            : (li < 4 ? npc->lines[li] : nullptr);
        if (line) {
            ren_.printW(2, boxY + 2, line,
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        }
        ren_.printW(2, boxY + 4, L"[ Z: 다음 ]",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        return;
    }

    // 조작 안내
    ren_.printW(2, boxY + 2, L"방향키: 이동  Z: 상호작용",
        std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    ren_.printW(W - 14, boxY + 2, L"[ M: 메뉴 ]",
        std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);

    // 파티 상태 (우측 상단)
    wchar_t buf[64];
    swprintf(buf, 64, L"파티: %d마리", pl_.partySize);
    ren_.printW(W - 12, 0, buf, std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
    if (pl_.partySize > 0) {
        const Pokemon& lead = pl_.party[firstAlive(pl_) >= 0 ? firstAlive(pl_) : 0];
        if (lead.species) {
            swprintf(buf, 64, L"%ls Lv.%d HP:%d/%d",
                lead.species->name, lead.level, lead.currentHP, lead.maxHP);
            ren_.printW(W - 24, 1, buf, std::string(Color::BG_BLACK) + Color::CYAN);
        }
    }
}
