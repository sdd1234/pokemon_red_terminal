#include "game.h"
#include "../data/sprites.h"
#include "../data/type_chart.h"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <wchar.h>
#include <cstdlib>
#include <ctime>
#ifndef swprintf
#define swprintf _snwprintf
#endif

static const int STARTER_IDS[3] = {1, 4, 7};

const wchar_t* Game::INTRO_LINES[Game::INTRO_COUNT] = {
    L"안녕! 포켓몬의 세계에 잘 왔어!",                                  // 0
    L"내 이름은 「오박사」. 사람들은 나를 포켓몬 박사라고도 부른단다.",  // 1
    L"이 세상엔 포켓몬이라 불리는 신비한 생물들이 함께 살고 있단다.",   // 2
    L"어떤 이에겐 포켓몬은 친구이고, 어떤 이는 시합 동료로 키우지.",    // 3
    L"나는 말이지... 포켓몬을 연구하는 사람이란다. 자, 이름이 뭐니?",  // 4  → NAME_INPUT
    nullptr,                                                             // 5  → "그래! 네 이름은 OO구나!" (동적)
    L"이 아이는 내 손자란다.",                                          // 6
    L"갓난아기 때부터 너의 라이벌이었지.",                              // 7
    L"...어라, 이 녀석 이름이 뭐였더라?",                                // 8  → RIVAL_NAME_INPUT
    nullptr,                                                             // 9  → "맞다! 이제 생각났어. 이 녀석 이름은 OO!" (동적)
    L"너만의 포켓몬 전설이 이제 막 펼쳐지려 하고 있어!",                 // 10
    L"꿈과 모험이 가득한 포켓몬의 세계로! 자, 가자!",                    // 11 → OVERWORLD
};

const wchar_t* Game::DEX_LINES[5] = {
    L"아, 잠깐! 줄 것이 있어!",
    L"이것이 포켓덱스! 만난 포켓몬을 자동으로 기록해줘.",
    L"그리고 몬스터볼도 5개 줄게.",
    L"야생 포켓몬은 약하게 만든 뒤 몬스터볼을 던져봐!",
    nullptr
};

const wchar_t* Game::RIVAL_INTERCEPT_LINES[Game::RIVAL_INTERCEPT_COUNT] = {
    L"블루: 잠깐, 거기!",
    L"블루: 우리 포켓몬 한 번 비교해보자!",
    L"블루: 너에게 골라줬으니, 내가 이길 거야!",
    L"블루: 자, 한 판 붙어보자!",
};

const wchar_t* Game::LAB_INTRO_LINES[Game::LAB_INTRO_COUNT] = {
    L"오박사: 잠깐! 거긴 위험해!",
    L"오박사: 키 큰 풀숲엔 야생 포켓몬이 잔뜩 있단다.",
    L"오박사: 자기 포켓몬도 없이 가면 큰일 나!",
    L"오박사: 자, 같이 내 연구소로 가자.",
    L"...... 오박사를 따라 연구소로 갔다 ......",
};

Game::Game() {
    srand((unsigned)time(nullptr));
    memset(&player_, 0, sizeof(player_));
    wcscpy(player_.name, L"RED");
    wcscpy(player_.rivalName, L"블루");
    player_.mapId = MAP_PALLET;
    player_.x = 11; player_.y = 7;
    player_.dir = 0;
    player_.bagSize = 0;
    player_.money = 3000;
    player_.hasPokedex = false;
    player_.starterIdx = -1;
    // 첫 게임 시작 — blackout 시 주인공 집으로
    player_.lastBlackoutMapId = MAP_PLAYER_HOUSE;
    player_.lastBlackoutX     = 5;
    player_.lastBlackoutY     = 3;
    player_.lastBlackoutDir   = 0;

    battle_ = nullptr;
    ow_     = nullptr;
}

void Game::run() {
    renderer.init();
    Audio::init();
    const int FRAME_MS = 62;

    while (running) {
        DWORD start = GetTickCount();

        if (scene_ == Scene::NAME_INPUT || scene_ == Scene::RIVAL_NAME_INPUT) {
            char ch = Input::pollChar();
            if (ch == 27) { running = false; continue; }
            if (scene_ == Scene::NAME_INPUT)       updateNameInput(Key::NONE, ch);
            else                                   updateRivalNameInput(Key::NONE, ch);
        } else {
            Key key = Input::poll();
            update(key);
        }

        updateBGM();      // 씬/맵에 맞는 BGM 선택 (같은 곡이면 내부에서 무시)
        Audio::update();  // 곡 끝나면 재시작 — 루프 보장

        renderer.clear();
        render();
        renderKorean();
        // 볼륨 변경 시 잠깐 표시 (모든 씬 위에 마지막으로 그림)
        if (volMsgTimer_ > 0) {
            wchar_t vb[32];
            swprintf(vb, 32, L"[ 볼륨 %d%% ]", Audio::volumePercent());
            renderer.printW(2, 0, vb, std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
            volMsgTimer_--;
        }
        renderer.flush();

        DWORD elapsed = GetTickCount() - start;
        if (elapsed < (DWORD)FRAME_MS)
            Sleep(FRAME_MS - elapsed);
        frame_++;
    }
}

void Game::changeScene(Scene next) {
    scene_ = next;
    frame_ = 0;
    renderer.redrawAll();
}

// 맵 ID → BGM 트랙명 (sounds/<name>.wav)
static const char* mapBGM(int id) {
    switch (id) {
    case MAP_PALLET:
    case MAP_PLAYER_HOUSE:
    case MAP_PLAYER_HOUSE2:           return "pallettown";
    case MAP_ROUTE1:
    case MAP_ROUTE2:
    case MAP_ROUTE2_GATE:             return "route";
    case MAP_VIRIDIAN:
    case MAP_PEWTER:
    case MAP_VIRIDIAN_MART:
    case MAP_PEWTER_MART:
    case MAP_VIRIDIAN_SCHOOL_HOUSE:
    case MAP_VIRIDIAN_NICKNAME_HOUSE: return "city";
    case MAP_OAK_LAB:                 return "oakslab";
    case MAP_PEWTER_GYM:              return "gym";
    case MAP_VIRIDIAN_PC:
    case MAP_PEWTER_PC:               return "pokecenter";
    default:                          return "pallettown";
    }
}

// 매 프레임: 현재 씬/맵에 맞는 BGM 을 고른다. playBGM 은 동일 곡이면 무시하므로
// 전이마다 일일이 호출하지 않아도 항상 올바른 곡이 재생된다.
void Game::updateBGM() {
    const char* t = nullptr;
    // 인트로 데모: 원작 Music_IntroBattle 을 1회만 재생(루프X) → 곡이 끝나면 타이틀로.
    if (scene_ == Scene::INTRO_MOVIE) { Audio::playBGM("introbattle", false); return; }
    switch (scene_) {
    case Scene::TITLE:
    case Scene::INTRO:
    case Scene::NAME_INPUT:
    case Scene::RIVAL_NAME_INPUT:     t = "title";          break;
    case Scene::LAB_INTRO:
    case Scene::STARTER_SELECT:
    case Scene::RIVAL_INTERCEPT:
    case Scene::RECEIVE_DEX:          t = "oakslab";        break;
    case Scene::OVERWORLD:
    case Scene::INGAME_MENU:
    case Scene::WARP_MENU:
    case Scene::MART_EVENT:
    case Scene::MART_SHOP:            t = mapBGM(player_.mapId); break;
    case Scene::WILD_BATTLE:          t = "wildbattle";     break;
    case Scene::TRAINER_BATTLE:       t = "trainerbattle";  break;
    case Scene::BOSS_BATTLE:          t = "gymleader";      break;
    case Scene::POKEMON_CENTER:       t = "pokecenter";     break;
    case Scene::ENDING:               t = "ending";         break;
    case Scene::GAME_OVER:            t = nullptr;          break;  // 무음
    default:                          t = nullptr;          break;
    }
    if (t) Audio::playBGM(t);
    else   Audio::stopBGM();
}

// ─── 씬 라우팅 ───────────────────────────────────────────────
void Game::update(Key key) {
    if (key == Key::ESCAPE) { running = false; return; }

    // 볼륨 조절 (+/-) — 어느 씬에서나 동작
    if (key == Key::VOL_UP)   { Audio::volumeUp();   volMsgTimer_ = 16; return; }
    if (key == Key::VOL_DOWN) { Audio::volumeDown(); volMsgTimer_ = 16; return; }

    switch (scene_) {
    case Scene::INTRO_MOVIE:       updateIntroMovie(key);     break;
    case Scene::TITLE:             updateTitle(key);          break;
    case Scene::INTRO:              updateIntro(key);          break;
    case Scene::NAME_INPUT:         /* handled separately */   break;
    case Scene::RIVAL_NAME_INPUT:   /* handled separately */   break;
    case Scene::LAB_INTRO:          updateLabIntro(key);       break;
    case Scene::STARTER_SELECT: updateStarterSelect(key);  break;
    case Scene::RIVAL_INTERCEPT: updateRivalIntercept(key);break;
    case Scene::RECEIVE_DEX:    updateReceiveDex(key);     break;
    case Scene::OVERWORLD: {
        if (!ow_) break;
        // Ctrl+M → 디버그 워프 메뉴 + (첫 1회) 가방에 모든 아이템을 정확히 99개씩 세팅
        if (key == Key::WARP_MENU) {
            if (!player_.cheatItemsGranted) {
                ItemId allItems[] = {ITEM_POTION, ITEM_POKE_BALL, ITEM_RARE_CANDY};
                for (ItemId id : allItems) {
                    int idx = findBagSlot(player_, id);
                    if (idx >= 0) {
                        player_.bag[idx].count = 99;
                    } else if (player_.bagSize < MAX_BAG_SLOTS) {
                        player_.bag[player_.bagSize].id    = id;
                        player_.bag[player_.bagSize].count = 99;
                        player_.bagSize++;
                    }
                }
                player_.cheatItemsGranted = true;
            }
            changeScene(Scene::WARP_MENU);
            warpCursor_ = 0;
            break;
        }
        if (key == Key::MENU) {
            prevScene_  = scene_;
            menuState_  = InGameMenuState::TOP_LEVEL;
            menuCursor_ = 0;
            Audio::playSE("se_menu");
            changeScene(Scene::INGAME_MENU);
            break;
        }
        ow_->update(key);
        OwEvent ev = ow_->popEvent();
        switch (ev) {
        case OwEvent::WILD_ENCOUNTER:
            if (!battle_) battle_ = new Battle(renderer, player_);
            battle_->startWild(ow_->wildSpeciesId(), ow_->wildLevel());
            changeScene(Scene::WILD_BATTLE);
            break;
        case OwEvent::TRAINER_BATTLE: {
            MapDef* m = getMap(player_.mapId);
            if (m && ow_->eventData() < m->numTrainers) {
                TrainerDef& tr = const_cast<TrainerDef&>(m->trainers[ow_->eventData()]);
                if (!battle_) battle_ = new Battle(renderer, player_);
                int pSize = tr.partyIds[2] ? 3 : (tr.partyIds[1] ? 2 : 1);
                int prize = tr.prize > 0 ? tr.prize : (tr.isBoss ? 1000 : 100);
                battle_->startTrainer(tr.name, tr.preBattleText,
                    tr.partyIds, tr.partyLevels, pSize, tr.introSpriteId, prize);
                changeScene(Scene::TRAINER_BATTLE);
            }
            break;
        }
        case OwEvent::BOSS_BATTLE:
            if (!battle_) battle_ = new Battle(renderer, player_);
            battle_->startBrock();
            changeScene(Scene::BOSS_BATTLE);
            break;
        case OwEvent::ENTER_POKEMON_CENTER:
            changeScene(Scene::POKEMON_CENTER);
            centerStep_ = 0;
            break;
        case OwEvent::NURSE_HEAL:
            healAll(player_);
            recordBlackoutPoint(player_);
            break;
        case OwEvent::ENTER_MART:
            if (!player_.gotParcel) {
                changeScene(Scene::MART_EVENT);
                martStep_ = 0;
            }
            break;
        case OwEvent::ENTER_MART_SHOP:
            shopMartId_   = ow_ ? ow_->eventData() : 0;
            shopCursor_   = 0;
            shopMode_     = 0;
            shopQuantity_ = 1;
            shopMsg_      = nullptr;
            shopMsgTimer_ = 0;
            changeScene(Scene::MART_SHOP);
            break;
        case OwEvent::OAK_INTERCEPT:
            if (ow_) ow_->startOakIntercept();
            break;
        case OwEvent::CUTSCENE_END_OAK:
            player_.mapId = MAP_OAK_LAB;
            player_.x = 5; player_.y = 10;   // 정문 안쪽
            player_.dir = 1;                  // 위 (오박사 향함)
            if (ow_) ow_->init();             // OW 재초기화
            break;
        case OwEvent::STARTER_TRIGGER:
            starterCursor_ = 0;
            changeScene(Scene::STARTER_SELECT);
            break;
        case OwEvent::CUTSCENE_END_RIVAL: {
            int rivalStarterIdx = (starterCursor_ + 1) % 3;
            int rivalIds[]  = {STARTER_IDS[rivalStarterIdx], 0, 0};
            int rivalLvls[] = {5, 0, 0};
            if (!battle_) battle_ = new Battle(renderer, player_);
            battle_->startTrainer(player_.rivalName, L"라이벌과의 첫 승부!",
                                  rivalIds, rivalLvls, 1);
            changeScene(Scene::TRAINER_BATTLE);
            break;
        }
        default: break;
        }
        break;
    }
    case Scene::WILD_BATTLE:
    case Scene::TRAINER_BATTLE:
    case Scene::BOSS_BATTLE:
        if (battle_) {
            battle_->update(key);
            if (battle_->isDone()) {
                BattleResult res = battle_->result();
                bool won = (res == BattleResult::WIN);

                if (scene_ == Scene::BOSS_BATTLE && won) {
                    player_.beatenBrock = true;
                    // 상금은 Battle에서 지급/표시함. 여기선 격파 처리만.
                    MapDef* mBoss = getMap(player_.mapId);
                    if (mBoss) {
                        int idx = ow_ ? ow_->eventData() : -1;
                        if (idx >= 0 && idx < mBoss->numTrainers)
                            const_cast<TrainerDef&>(mBoss->trainers[idx]).defeated = true;
                    }
                    changeScene(Scene::ENDING);
                    endingStep_ = 0;
                    delete battle_; battle_ = nullptr;
                    break;
                }

                if (scene_ == Scene::TRAINER_BATTLE && won) {
                    // 상금은 Battle에서 지급/표시함. 여기선 격파 처리만.
                    MapDef* m = getMap(player_.mapId);
                    if (m) {
                        int idx = ow_ ? ow_->eventData() : -1;
                        if (idx >= 0 && idx < m->numTrainers)
                            const_cast<TrainerDef&>(m->trainers[idx]).defeated = true;
                    }
                }

                if (!won && res == BattleResult::LOSE) {
                    // 전멸 → 마지막 회복 지점(포켓몬센터/주인공집)으로 + 돈 절반 (원본 동작)
                    healAll(player_);
                    player_.money /= 2;
                    player_.mapId = player_.lastBlackoutMapId;
                    player_.x     = player_.lastBlackoutX;
                    player_.y     = player_.lastBlackoutY;
                    player_.dir   = player_.lastBlackoutDir;
                    if (ow_) ow_->init();
                    changeScene(Scene::GAME_OVER);
                } else if (!player_.beatenRival1 && won && scene_ == Scene::TRAINER_BATTLE) {
                    // 라이벌 첫 배틀 격파 → 포켓덱스 수령
                    // (야생 포획도 WIN 결과지만 WILD_BATTLE 씬이라 여기 안 들어옴)
                    player_.beatenRival1 = true;
                    dexStep_ = 0;
                    changeScene(Scene::RECEIVE_DEX);
                } else {
                    if (ow_) ow_->onReturnFromBattle(won);
                    else {
                        if (!ow_) ow_ = new Overworld(renderer, player_);
                        ow_->init();
                    }
                    changeScene(Scene::OVERWORLD);
                }
                delete battle_; battle_ = nullptr;
            }
        }
        break;
    case Scene::POKEMON_CENTER:
        updateCenter(key);
        break;
    case Scene::MART_EVENT:
        updateMart(key);
        break;
    case Scene::MART_SHOP:
        updateMartShop(key);
        break;
    case Scene::GAME_OVER:
        updateGameOver(key);
        break;
    case Scene::ENDING:
        updateEnding(key);
        break;
    case Scene::WARP_MENU:
        updateWarpMenu(key);
        break;
    case Scene::INGAME_MENU:
        updateInGameMenu(key);
        break;
    default: break;
    }
}

void Game::render() {
    switch (scene_) {
    case Scene::INTRO_MOVIE:       renderIntroMovie();     break;
    case Scene::TITLE:             renderTitle();          break;
    case Scene::INTRO:              renderIntro();          break;
    case Scene::NAME_INPUT:         renderNameInput();      break;
    case Scene::RIVAL_NAME_INPUT:   renderRivalNameInput(); break;
    case Scene::LAB_INTRO:          renderLabIntro();       break;
    case Scene::STARTER_SELECT:     renderStarterSelect();  break;
    case Scene::RIVAL_INTERCEPT:    renderRivalIntercept(); break;
    case Scene::RECEIVE_DEX:    renderReceiveDex();     break;
    case Scene::OVERWORLD:
        if (ow_) ow_->render();
        break;
    case Scene::WILD_BATTLE:
    case Scene::TRAINER_BATTLE:
    case Scene::BOSS_BATTLE:
        if (battle_) battle_->render();
        break;
    case Scene::POKEMON_CENTER: renderCenter();         break;
    case Scene::MART_EVENT:     renderMart();           break;
    case Scene::MART_SHOP:      renderMartShop();       break;
    case Scene::GAME_OVER:      renderGameOver();       break;
    case Scene::ENDING:         renderEnding();         break;
    case Scene::INGAME_MENU:    renderInGameMenu();     break;
    default: break;
    }
}

void Game::renderKorean() {
    switch (scene_) {
    case Scene::INTRO_MOVIE:       renderIntroMovieKorean();     break;
    case Scene::TITLE:             renderTitleKorean();          break;
    case Scene::INTRO:              renderIntroKorean();          break;
    case Scene::NAME_INPUT:         renderNameInputKorean();      break;
    case Scene::RIVAL_NAME_INPUT:   renderRivalNameInputKorean(); break;
    case Scene::LAB_INTRO:          renderLabIntroKorean();       break;
    case Scene::STARTER_SELECT:     renderStarterSelectKorean();  break;
    case Scene::RIVAL_INTERCEPT:    renderRivalInterceptKorean(); break;
    case Scene::RECEIVE_DEX:    renderReceiveDexKorean();     break;
    case Scene::OVERWORLD:
        if (ow_) ow_->renderKorean();
        break;
    case Scene::WILD_BATTLE:
    case Scene::TRAINER_BATTLE:
    case Scene::BOSS_BATTLE:
        if (battle_) battle_->renderKorean();
        break;
    case Scene::POKEMON_CENTER: renderCenterKorean();         break;
    case Scene::MART_EVENT:     renderMartKorean();           break;
    case Scene::MART_SHOP:      renderMartShopKorean();       break;
    case Scene::ENDING:         renderEndingKorean();         break;
    case Scene::WARP_MENU:      renderWarpMenuKorean();       break;
    case Scene::INGAME_MENU:    renderInGameMenuKorean();     break;
    default: break;
    }
}

// ─── 디버그 워프 메뉴 (Ctrl+M) ────────────────────────────
struct WarpMenuEntry { int mapId; int x; int y; const wchar_t* name; };
static const WarpMenuEntry WARP_MAPS[] = {
    {0,  10, 10, L"팔레트시티"},
    {1,  10, 30, L"1번도로"},
    {2,  20, 30, L"상록시티"},
    {3,  10, 60, L"2번도로"},
    {4,  16, 40, L"상록숲"},
    {5,  20, 30, L"회색시티"},
    {6,  5,  10, L"오박사 연구소"},
    {7,  3,  6,  L"주인공의 집 1F"},
    {8,  4,  12, L"회색시티 체육관"},
    {9,  3,  6,  L"주인공의 방 2F"},
    {10, 3,  6,  L"라이벌의 집"},
    {11, 3,  7,  L"상록 포센"},
    {12, 3,  7,  L"펠터 포센"},
    {13, 3,  7,  L"상록 마트"},
    {14, 3,  7,  L"펠터 마트"},
    {15, 4,  7,  L"2번도로 게이트"},
    {16, 4,  7,  L"상록숲 북쪽 게이트"},
    {17, 4,  7,  L"상록숲 남쪽 게이트"},
};
static const int WARP_MAPS_COUNT = sizeof(WARP_MAPS) / sizeof(WARP_MAPS[0]);

void Game::updateWarpMenu(Key key) {
    if (key == Key::UP) {
        warpCursor_ = (warpCursor_ - 1 + WARP_MAPS_COUNT) % WARP_MAPS_COUNT;
    } else if (key == Key::DOWN) {
        warpCursor_ = (warpCursor_ + 1) % WARP_MAPS_COUNT;
    } else if (key == Key::A) {
        const WarpMenuEntry& w = WARP_MAPS[warpCursor_];
        player_.mapId = w.mapId;
        player_.x = w.x;
        player_.y = w.y;

        if (!playerHasCut(player_) && player_.partySize < 6) {
            Pokemon dummy = makePokemon(1, 10);
            if (dummy.species) {
                dummy.moves[0].moveId = MOVE_CUT;
                dummy.moves[0].pp     = getMoveData(MOVE_CUT).maxPP;
                if (dummy.numMoves < 1) dummy.numMoves = 1;
                player_.party[player_.partySize++] = dummy;
            }

            // 레벨업 직전 테스트 포켓몬 (다음 레벨이 3의 배수 → 기술 교체 팝업 테스트용)
            if (player_.partySize < 6) {
                Pokemon test = makePokemon(4, 8);  // 파이리 Lv8 (다음 Lv9 = 3의 배수)
                if (test.species) {
                    // 기술 4칸 가득 채우기 (교체 팝업 유발)
                    int fillers[] = {1, 4};  // 몸통박치기, 물총
                    for (int f = 0; f < 2 && test.numMoves < 4; f++) {
                        bool has = false;
                        for (int j = 0; j < test.numMoves; j++)
                            if (test.moves[j].moveId == fillers[f]) { has = true; break; }
                        if (!has) {
                            test.moves[test.numMoves].moveId = fillers[f];
                            test.moves[test.numMoves].pp     = getMoveData(fillers[f]).maxPP;
                            test.numMoves++;
                        }
                    }
                    // 다음 레벨 직전까지 경험치 충전 → 한 번 이기면 바로 레벨업
                    test.exp = expForLevel(test.level + 1) - 1;
                    player_.party[player_.partySize++] = test;
                }
            }
        }

        if (ow_) ow_->init();
        changeScene(Scene::OVERWORLD);
    } else if (key == Key::B || key == Key::ESCAPE) {
        changeScene(Scene::OVERWORLD);
    }
}

void Game::renderWarpMenuKorean() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0, 0, W, H, ' ', std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.printW(2, 1, L"=== 디버그 워프 메뉴 (Ctrl+M) ===",
        std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(2, 2, L"위/아래: 선택  A(Z/Enter): 확인  B/ESC: 취소",
        std::string(Color::BG_BLACK)+Color::WHITE);
    for (int i = 0; i < WARP_MAPS_COUNT; i++) {
        const wchar_t* arrow = (i == warpCursor_) ? L"▶ " : L"  ";
        wchar_t buf[80];
        swprintf(buf, 80, L"%lsMAP_%d  %ls", arrow, WARP_MAPS[i].mapId, WARP_MAPS[i].name);
        std::string color = (i == warpCursor_)
            ? std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW
            : std::string(Color::BG_BLACK)+Color::WHITE;
        renderer.printW(2, 4 + i, buf, color);
    }
}

// 정면 몬스터 스프라이트(SpriteData) 출력 — 인트로/타이틀용 (배틀 drawSprite 동일 포맷)
static void drawFrontSprite(Renderer& r, const SpriteData& spr, int x, int y) {
    for (int i = 0; i < spr.height; i++)
        if (spr.rows[i]) r.printRaw(x, y + i, spr.rows[i]);
}

// 인트로 무비 오버레이(raw ANSI 행 배열) 출력
static void drawRows(Renderer& r, const char* rows[], int h, int x, int y) {
    if (y < 0) y = 0;
    for (int i = 0; i < h; i++)
        if (rows[i]) r.printRaw(x, y + i, rows[i]);
}

// 니도리노 HIP/HOP — 앞뒤로 오가며 무지개형 포물선으로 뛴다.
// 24프레임 = 앞으로(팬텀쪽) 한 번 아치 + 뒤로 한 번 아치.
static int hopX(int f) {          // 수평: 0→앞(-8)→0 (팬텀쪽 갔다 복귀)
    int p = f % 24;               // 0..23
    return (p < 12) ? -(8 * p / 12) : -(8 * (24 - p) / 12);
}
static int hopY(int f) {          // 수직: 12프레임마다 포물선 아크(최고 8)
    int p = f % 12;               // 0..11
    int d = p - 6;                // -6..5
    return 8 * (36 - d * d) / 36;  // p=6 최고 8, p=0/12 착지
}

// ─── 오프닝 애니메이션 (원작 PlayIntro: 니도리노 vs 팬텀) ──────────────────────
// frame_ 기반. 인트로곡(introbattle)이 1회 재생을 마치면 타이틀로 넘어간다.
// 아무 키나 누르면 즉시 스킵(원작 CheckForUserInterruption).
// 실제 안무/타임라인은 renderIntroMovie 주석 참고.
static const int IM_FALLBACK_END = 122;   // 오디오 실패(파일 없음 등) 시 폴백 길이
static const int IM_HARD_CAP     = 400;   // 안전 상한 (mode 보고 이상 시)

void Game::updateIntroMovie(Key key) {
    bool finished  = Audio::bgmFinished();   // 인트로곡 1회 재생 종료
    // BGM 은 updateBGM(이 함수 뒤 호출)에서 시작 → 초반 몇 프레임은 미재생. frame_>=8 후 판단.
    bool audioDead = frame_ >= 8 && !Audio::bgmPlaying() && !finished;
    if (key == Key::A || key == Key::START || finished ||
        (audioDead && frame_ >= IM_FALLBACK_END) || frame_ >= IM_HARD_CAP) {
        changeScene(Scene::TITLE);
        return;
    }
    switch (frame_) {            // 효과음 (해당 프레임 1회) — 원작 intro.asm SFX 대응
    case 18: Audio::playSE("se_cursor"); break;  // SFX_INTRO_HIP (점프)
    case 30: Audio::playSE("se_cursor"); break;  // SFX_INTRO_HOP (착지)
    case 40: Audio::playSE("se_menu");   break;  // SFX_INTRO_RAISE (팬텀 팔 듦)
    case 58: Audio::playSE("se_bump");   break;  // SFX_INTRO_CRASH (슬래시 임팩트)
    case 68: Audio::playSE("se_cursor"); break;  // SFX_INTRO_HIP (점프)
    case 82: Audio::playSE("se_bump");   break;  // SFX_INTRO_LUNGE (물기 돌진)
    default: break;
    }
}

// 원작 PlayIntro 그대로 — gfx/intro/red_nidorino_1~3(니도리노 OAM) + gfx/intro/gengar(배경 실루엣 3포즈).
// 흰 배경 위 어두운 실루엣(원작 동일). 니도리노 좌하단(뒷모습)에서 등장, 팬텀 우상단 큰 실루엣.
//  0~16  니도리노 슬라이드 인(좌→제자리)   16~40  제자리 포물선 점프(공중엔 대기 프레임)
// 40~52  팬텀 raise(포즈1)                  52~66  팬텀 slash(포즈2)+니도리노 반격점프(프레임3)
// 66~82  팬텀 복귀+바운스                   82~113 니도리노 런지(물기)
// 113~   최종 물기 장면 유지 → introbattle 곡이 끝나면 updateIntroMovie 에서 타이틀로
void Game::renderIntroMovie() {
    int W = renderer.width, H = renderer.height;
    int f = frame_;

    // 원작 인트로 배경 = 흰색 (어두운 실루엣 대비)
    renderer.fillRect(0, 0, W, H, ' ', std::string(Color::BG_WHITE)+Color::WHITE);
    int cx = W / 2, cy = H / 2;

    // 유튜브 원작 확인: 왼쪽=팬텀(공격), 오른쪽=니도리노(스프라이트가 좌향이라 왼쪽 팬텀을 봄, 미러 불필요)
    int nidBaseX = cx + 10;  int nidBaseY = cy + 2;        // 니도리노 48×24, 우측
    int genX     = cx - 58;                                // 팬텀 56×28, 좌측(큼)
    int genY     = nidBaseY + INTRO_NIDORINO_H - INTRO_GENGAR_H;  // 둘이 같은 바닥선(일직선)
    if (genY < 0) genY = 0;
    if (nidBaseY < 0) nidBaseY = 0;

    int nidX = nidBaseX, nidY = nidBaseY;
    int nidFrame = 0, genPose = 0;
    bool nidHide = false;   // 피격 깜빡임
    // 프레임 의미(원작): 0=대기, 1=피격/리코일(FightIntroFrontMon2), 2=물기/런지(FightIntroFrontMon3)

    if (f < 16) {                                 // P1 니도리노 오른쪽서 슬라이드 인
        nidX = W - (W - nidBaseX) * f / 16;
    } else if (f < 40) {                          // P2 HIP/HOP — 앞뒤로 오가며 포물선 점프
        nidX += hopX(f);                          // 팬텀쪽 갔다 복귀
        nidY -= hopY(f);                          // 무지개형 아크(공중엔 대기 프레임)
        nidFrame = 0;                             // 공중에서 발길질(피격 포즈) 안 함
    } else if (f < 52) {                          // P3 팬텀 raise(팔 듦)
        genPose = 1;
    } else if (f < 66) {                          // P4 팬텀 slash(오른쪽 후려침) → 니도리노 피격
        genPose = 2;
        int q = f - 52;                           // 0..13
        genX += (q < 7) ? q : (14 - q);           // 팬텀 오른쪽(니도리노 쪽)으로 후려치고 복귀
        nidFrame = 1;                             // 피격 포즈
        if (q >= 6) {                             // 임팩트! 피격 반응
            int k = q - 6;                        // 0..7
            nidY -= (k < 4) ? (8 - 2 * k) : 0;    // 위로 튕겼다 착지
            nidX += (k < 6) ? (6 - k) : 0;        // 오른쪽으로 넉백(팬텀 반대로) 후 복귀
            nidHide = (k < 4) && (k % 2 == 1);    // 피격 깜빡임
        }
    } else if (f < 82) {                          // P5 팬텀 복귀 + 니도리노 앞뒤 포물선 점프
        nidX += hopX(f);
        nidY -= hopY(f);
        nidFrame = 0;
    } else {                                      // P6 니도리노 점프해서 물기 — 팬텀에 파고들어 가려진 채 끝(복귀 없음)
        int span = 31, q = f - 82;                // 82~113
        if (q > span) q = span;
        int target = genX + 24;                   // 팬텀 몸에 깊숙이 겹치는 지점
        nidX = nidBaseX - (nidBaseX - target) * q / span;   // 오른쪽 → 팬텀으로 (단조 돌진)
        int d = q - span / 2; if (d < 0) d = -d;
        nidY = nidBaseY - 10 * (span / 2 - d) / (span / 2); // 점프 아크(중간 최고점→착지=무는 순간)
        nidFrame = 2;                             // 무는 프레임
    }

    // 그리기 순서: 평소엔 팬텀(배경)→니도리노(전경). 단 물기 돌진(P6)엔 니도리노가
    // 팬텀에 파고들어 "가려진" 상태가 되도록 팬텀을 위에 덮어 그림.
    if (f >= 82) {
        if (!nidHide) drawRows(renderer, INTRO_NIDORINO[nidFrame], INTRO_NIDORINO_H, nidX, nidY);
        drawRows(renderer, INTRO_GENGAR[genPose], INTRO_GENGAR_H, genX, genY);
    } else {
        drawRows(renderer, INTRO_GENGAR[genPose], INTRO_GENGAR_H, genX, genY);
        if (!nidHide) drawRows(renderer, INTRO_NIDORINO[nidFrame], INTRO_NIDORINO_H, nidX, nidY);
    }
}

void Game::renderIntroMovieKorean() {
    int H = renderer.height;
    renderer.printW(2, H - 1, L"아무 키나 누르면 건너뛰기",
        std::string(Color::BG_WHITE) + "\033[38;5;240m");
}

// ─── 타이틀 화면 (원작 PlayIntro 종료 후 타이틀 = 로고 + 타이틀 몬 + PRESS START) ──
void Game::updateTitle(Key key) {
    if (key == Key::A || key == Key::START) {
        introStep_ = 0;
        changeScene(Scene::INTRO);   // 오박사 스피치 시작
    }
}

void Game::renderTitle() {
    int W = renderer.width, H = renderer.height;
    // 원작 GB 타이틀은 모노크롬 — 회색(244) 바탕에 흑백 로고/몬/레드(스프라이트 회색박스가 녹음)
    std::string gray = "\033[48;5;244m";
    std::string dark = "\033[38;5;232m";
    renderer.fillRect(0, 0, W, H, ' ', gray + dark);
    int cx = W / 2;

    // 포켓몬 로고 그래픽 (128×28, 흑백 — 색은 데이터에 구워짐)
    int logoX = cx - TITLE_LOGO_W / 2; if (logoX < 0) logoX = 0;
    int logoY = 2;
    for (int i = 0; i < TITLE_LOGO_H && logoY + i < H; i++)
        renderer.printRaw(logoX, logoY + i, TITLE_LOGO[i]);

    // RED VERSION 그래픽 (80×4) — 로고 바로 아래
    int verY = logoY + TITLE_LOGO_H + 1;
    int verX = cx - TITLE_VERSION_W / 2; if (verX < 0) verX = 0;
    for (int i = 0; i < TITLE_VERSION_H && verY + i < H; i++)
        renderer.printRaw(verX, verY + i, TITLE_VERSION[i]);

    // ── 순환 몬 + 레드(주인공) 나란히 — 원작 title.asm: DrawPlayerCharacter + 순환 몬 + 손에 볼 ──
    static const int TITLE_MONS[] = { 4, 7, 1, 13, 25, 95 };  // 파이리 먼저 순환(title_mons.asm)
    const int nMons = (int)(sizeof(TITLE_MONS) / sizeof(TITLE_MONS[0]));
    const SpriteData* mon = getSpriteFront(TITLE_MONS[(frame_ / 48) % nMons]);
    int bandY = verY + TITLE_VERSION_H + 1;          // 몬/레드 공통 윗줄
    int monX  = cx - 40;                              // 몬: 왼쪽
    int redX  = cx + 8;                               // 레드: 오른쪽
    if (mon) drawFrontSprite(renderer, *mon, monX, bandY);
    drawRows(renderer, TITLE_RED, TITLE_RED_H, redX, bandY);

    // ©GAME FREAK (하단 고정)
    renderer.print(cx - 11, H - 2, "(C)1996 GAME FREAK inc.", gray + "\033[38;5;240m");

    // PRESS START 깜빡임 (하단 고정)
    if ((frame_ / 16) % 2 == 0)
        renderer.print(cx - 8, H - 4, ">> PRESS START <<", gray + dark);
}

void Game::renderTitleKorean() {
    // 한글 오버레이 없음 (원작 타이틀은 PRESS START 단일 — 깔끔히 유지)
}

// ─── 인트로 씬 ───────────────────────────────────────────────
void Game::updateIntro(Key key) {
    if (key != Key::A) return;

    // step 4: "이름을 알려주세요" → NAME_INPUT
    if (introStep_ == 4) {
        changeScene(Scene::NAME_INPUT);
        nameLen_ = 0;
        memset(nameBuf_, 0, sizeof(nameBuf_));
        return;
    }
    // step 8: "이름이 뭐였더라?" → RIVAL_NAME_INPUT
    if (introStep_ == 8) {
        changeScene(Scene::RIVAL_NAME_INPUT);
        rivalNameLen_ = 0;
        memset(rivalNameBuf_, 0, sizeof(rivalNameBuf_));
        return;
    }
    // step 11 (마지막): → OVERWORLD (주인공 집 2층 침실에서 깨어남 — 원작과 동일)
    if (introStep_ == 11) {
        player_.mapId = MAP_PLAYER_HOUSE2;
        player_.x = 4; player_.y = 4;   // 침대 옆 floor
        player_.dir = 0;
        player_.justWokeUp = false;     // 깨어나는 대사 X — pokered 원작처럼 그냥 등장
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        changeScene(Scene::OVERWORLD);
        return;
    }

    introStep_++;
    renderer.redrawAll();
}

// 인트로 트레이너 풀바디 스프라이트 출력 (28chars × 9rows)
static void drawIntroSprite(Renderer& r, const IntroSprite& spr, int x, int y) {
    for (int i = 0; i < INTRO_SPR_H; i++) {
        if (spr.rows[i]) r.printRaw(x, y + i, spr.rows[i]);
    }
}

void Game::renderIntro() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);

    int cx = W/2;
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;     // 대화창 위치

    // 스프라이트는 대화창 바로 위에 위치 (하단 5행은 대화창 뒤로 가려짐)
    int sprY = bY - INTRO_SPR_H + 5;
    if (sprY < 1) sprY = 1;
    int halfW = INTRO_SPR_W / 2;

    // ── 스프라이트 ──────────────────────────────────────────────
    // 스피치 순서: 오박사(0~3) → 레드(4~5) → 라이벌(6~9) → 레드(10~11)
    // (원작은 2~3에 니도리노를 끼우나, 회색 박스 깨짐 탓에 오박사로 통일)
    if (introStep_ <= 3) {
        // 0~3 오박사 (원작은 2~3에서 니도리노 정면을 보여주나, 본 클론은 오박사로 통일)
        drawIntroSprite(renderer, SPR_INTRO_OAK, cx - halfW, sprY);
    } else if (introStep_ == 4 || introStep_ == 5) {
        drawIntroSprite(renderer, SPR_INTRO_RED, cx - halfW, sprY);
    } else if (introStep_ <= 9) {
        drawIntroSprite(renderer, SPR_INTRO_RIVAL, cx - halfW, sprY);
    } else {  // 10, 11
        drawIntroSprite(renderer, SPR_INTRO_RED, cx - halfW, sprY);
    }

    // ── 대화창 ───────────────────────────────────────────────────
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    const char* speaker = "[OAK]";
    renderer.print(bX+2,bY+1,speaker,
        std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ",
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.print(bX+2,bY+bH-2,"[ Z / Enter: 다음 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

void Game::renderIntroKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (introStep_ >= INTRO_COUNT) return;

    // (스프라이트는 renderIntro에서 그려짐 — 대화창 뒤 레이어)

    // ── 대화창 텍스트 ─────────────────────────────────────────
    // step 5: 이름 확인 (pokered _YourNameIsText: "Right! So your name is X!")
    if (introStep_ == 5) {
        wchar_t buf[64];
        swprintf(buf, 64, L"그래! 네 이름은 %ls구나!", player_.name);
        renderer.printW(bX+2, bY+3, buf, std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
        return;
    }
    // step 9: 라이벌 이름 확인 (pokered _HisNameIsText: "That's right! I remember now! His name is X!")
    if (introStep_ == 9) {
        wchar_t buf[64];
        swprintf(buf, 64, L"맞다! 이제 생각났어. 이 녀석 이름은 %ls!", player_.rivalName);
        renderer.printW(bX+2, bY+3, buf, std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
        return;
    }

    if (INTRO_LINES[introStep_])
        renderer.printW(bX+2, bY+3, INTRO_LINES[introStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

// ─── 이름 입력 ───────────────────────────────────────────────
void Game::updateNameInput(Key, char ch) {
    if (ch == 13) { // Enter → 확정
        if (nameLen_ == 0) {
            wcscpy(player_.name, L"RED");
        } else {
            for (int i = 0; i < nameLen_ && i < 8; i++)
                player_.name[i] = (wchar_t)(unsigned char)nameBuf_[i];
            player_.name[nameLen_] = 0;
        }
        // 인트로 step 5로 → "그렇군요! [이름]이군요!"
        introStep_ = 5;
        changeScene(Scene::INTRO);
    } else if (ch == 8) { // Backspace
        if (nameLen_ > 0) nameBuf_[--nameLen_] = 0;
    } else if (ch >= 32 && ch <= 126 && nameLen_ < 8) {
        nameBuf_[nameLen_++] = ch;
        nameBuf_[nameLen_]   = 0;
    }
}

void Game::renderNameInput() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=8, bY=H/2-4, bW=40, bX=(W-40)/2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
}

void Game::renderNameInputKorean() {
    int W = renderer.width, H = renderer.height;
    int bY=H/2-4, bX=(W-40)/2;
    renderer.printW(bX+2, bY+2, L"네 이름을 알려주겠니? (최대 8자)",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    // 현재 입력 표시
    wchar_t disp[64] = L"> ";
    for (int i = 0; i < nameLen_; i++) disp[2+i] = (wchar_t)(unsigned char)nameBuf_[i];
    disp[2+nameLen_] = 0;
    if ((frame_/8)%2==0) { disp[2+nameLen_]='_'; disp[3+nameLen_]=0; }
    renderer.printW(bX+2, bY+4, disp, std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(bX+2, bY+6, L"[ Enter: 확인 / 비우면 RED ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 라이벌 이름 입력 ────────────────────────────────────────
void Game::updateRivalNameInput(Key, char ch) {
    if (ch == 13) { // Enter
        if (rivalNameLen_ == 0) {
            wcscpy(player_.rivalName, L"블루");
        } else {
            for (int i = 0; i < rivalNameLen_ && i < 8; i++)
                player_.rivalName[i] = (wchar_t)(unsigned char)rivalNameBuf_[i];
            player_.rivalName[rivalNameLen_] = 0;
        }
        // 인트로 step 9로 → "맞아! [라이벌]이야!"
        introStep_ = 9;
        changeScene(Scene::INTRO);
    } else if (ch == 8) {
        if (rivalNameLen_ > 0) rivalNameBuf_[--rivalNameLen_] = 0;
    } else if (ch >= 32 && ch <= 126 && rivalNameLen_ < 8) {
        rivalNameBuf_[rivalNameLen_++] = ch;
        rivalNameBuf_[rivalNameLen_]   = 0;
    }
}

void Game::renderRivalNameInput() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=8, bY=H/2-4, bW=40, bX=(W-40)/2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
}

void Game::renderRivalNameInputKorean() {
    int W = renderer.width, H = renderer.height;
    int bY=H/2-4, bX=(W-40)/2;
    renderer.printW(bX+2, bY+2, L"라이벌의 이름은 뭘로 할까? (최대 8자)",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    wchar_t disp[64] = L"> ";
    for (int i = 0; i < rivalNameLen_; i++)
        disp[2+i] = (wchar_t)(unsigned char)rivalNameBuf_[i];
    disp[2+rivalNameLen_] = 0;
    if ((frame_/8)%2==0) { disp[2+rivalNameLen_]='_'; disp[3+rivalNameLen_]=0; }
    renderer.printW(bX+2, bY+4, disp, std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(bX+2, bY+6, L"[ Enter: 확인 / 비우면 블루 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 스타터 선택 ─────────────────────────────────────────────
// (STARTER_IDS는 파일 상단으로 이동)
static const wchar_t* STARTER_NAMES[3] = {L"이상해씨", L"파이리", L"꼬부기"};
static const wchar_t* STARTER_TYPES[3] = {L"풀/독", L"불꽃", L"물"};

void Game::updateStarterSelect(Key key) {
    if (key == Key::LEFT)  starterCursor_ = (starterCursor_+2)%3;
    if (key == Key::RIGHT) starterCursor_ = (starterCursor_+1)%3;
    if (key == Key::A) {
        // 스타터 지급
        player_.party[0] = makePokemon(STARTER_IDS[starterCursor_], 5);
        player_.partySize = 1;
        player_.starterIdx = starterCursor_;
        // 원작: 스타터 받고 출구 향하면 블루가 가로막아 배틀.
        // 우리는 연구소 OW로 돌아간 뒤 즉시 RIVAL_BLOCK cutscene을 실행.
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        ow_->startRivalBlock();
        changeScene(Scene::OVERWORLD);
    }
}

// ─── 라이벌 인터셉트 ─────────────────────────────────────────
void Game::updateRivalIntercept(Key key) {
    if (key != Key::A) return;
    rivalInterceptStep_++;
    if (rivalInterceptStep_ >= RIVAL_INTERCEPT_COUNT) {
        // 블루는 상성 포켓몬 선택 (원작 동일)
        int rivalStarterIdx = (starterCursor_ + 1) % 3;
        int rivalIds[]  = {STARTER_IDS[rivalStarterIdx], 0, 0};
        int rivalLvls[] = {5, 0, 0};
        if (!battle_) battle_ = new Battle(renderer, player_);
        battle_->startTrainer(player_.rivalName, L"라이벌과의 첫 승부!",
                              rivalIds, rivalLvls, 1);
        changeScene(Scene::TRAINER_BATTLE);
    }
}

void Game::renderRivalIntercept() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);
    int cx = W/2;
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    int sprY = bY - INTRO_SPR_H + 5;
    if (sprY < 1) sprY = 1;
    int halfW = INTRO_SPR_W / 2;
    // 블루 풀바디
    for (int i = 0; i < INTRO_SPR_H; i++)
        if (SPR_INTRO_RIVAL.rows[i])
            renderer.printRaw(cx - halfW, sprY + i, SPR_INTRO_RIVAL.rows[i]);
    // 대화창
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[BLUE]", std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ", std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

void Game::renderRivalInterceptKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (rivalInterceptStep_ < RIVAL_INTERCEPT_COUNT)
        renderer.printW(bX+2, bY+3, RIVAL_INTERCEPT_LINES[rivalInterceptStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

void Game::renderStarterSelect() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);
    // sprite 사이즈는 종족별로 동일 (40×20). 3개를 가로로 배치.
    // 헤더 텍스트(상단 4줄)가 sprite 박스와 겹치지 않게 tableY를 row 7로 fix.
    int slotW = W / 3;
    int tableY = 7;
    for (int i = 0; i < 3; i++) {
        const SpriteData* spr = getSpriteFront(STARTER_IDS[i]);
        int sw = spr ? spr->width  : SPR_W;
        int sh = spr ? spr->height : SPR_H;
        int slotCx = slotW * i + slotW / 2;
        int cx = slotCx - sw / 2;
        if (cx < 1) cx = 1;
        std::string bord = (i == starterCursor_) ?
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW :
            std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK;
        renderer.drawBox(cx-1, tableY-1, sw+2, sh+3, bord);
        if (spr) {
            for (int r = 0; r < sh; r++) {
                if (spr->rows[r])
                    renderer.printRaw(cx, tableY+r, spr->rows[r]);
            }
        }
        if (i == starterCursor_)
            renderer.print(slotCx - 1, tableY+sh+1, "^^^",
                std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    }
}

void Game::renderStarterSelectKorean() {
    int W = renderer.width;
    int slotW = W / 3;
    int tableY = 7;  // renderStarterSelect와 일치
    // 헤더는 화면 상단 row 1~4 고정 (sprite 박스와 겹치지 않게)
    renderer.printW(W/2-10, 1, L"[ 오박사 연구소 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    renderer.printW(W/2-12, 2, L"블루가 지켜보고 있다...",
        std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
    renderer.printW(W/2-8, 3, L"포켓몬을 선택하세요!",
        std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(W/2-14, 4, L"← → 방향키로 선택, Z로 결정",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
    // 이름/타입 — sprite 박스 아래 (tableY + SPR_H + 여백)
    int nameY = tableY + SPR_H + 3;
    for (int i = 0; i < 3; i++) {
        int slotCx = slotW * i + slotW / 2;
        int cx = slotCx - 4;
        std::string col = (i==starterCursor_) ?
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW :
            std::string(Color::BG_BLACK)+Color::WHITE;
        renderer.printW(cx, nameY, STARTER_NAMES[i], col);
        renderer.printW(cx, nameY+1, STARTER_TYPES[i],
            std::string(Color::BG_BLACK)+Color::BRIGHT_GREEN);
    }
}

// ─── 포켓덱스 수령 ───────────────────────────────────────────
void Game::updateReceiveDex(Key key) {
    if (key == Key::A) {
        dexStep_++;
        if (dexStep_ >= 4 || !DEX_LINES[dexStep_]) {
            // 오버월드로 전환 — 플레이어는 연구소 안에 그대로 있어야 함 (원본은 직접 걸어 나감).
            // 오박사(5,3) 와 라이벌 자리(4,8) 사이 floor (5,8) 로 배치.
            player_.hasPokedex  = true;
            addItem(player_, ITEM_POKE_BALL, 5);
            player_.mapId = MAP_OAK_LAB;
            player_.x = 5; player_.y = 8;
            player_.dir = 0;              // 아래 = 문 향함
            if (!ow_) ow_ = new Overworld(renderer, player_);
            ow_->init();
            changeScene(Scene::OVERWORLD);
        }
    }
}

void Game::renderReceiveDex() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[OAK]", std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ",
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

void Game::renderReceiveDexKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (dexStep_ < 4 && DEX_LINES[dexStep_])
        renderer.printW(bX+2, bY+3, DEX_LINES[dexStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

// ─── 오박사 인터셉트 시퀀스 ──────────────────────────────────
// 풀숲에서 5단계 대사 후 → 연구소 OVERWORLD 진입.
// 연구소 안에서 플레이어가 오박사 NPC와 직접 상호작용 → STARTER_SELECT.
void Game::updateLabIntro(Key key) {
    if (key != Key::A) return;
    labIntroStep_++;
    if (labIntroStep_ >= LAB_INTRO_COUNT) {
        // 연구소 OW 진입 — 입구(5,11)에 도착, 위쪽 오박사를 향해 걷도록
        player_.mapId = MAP_OAK_LAB;
        player_.x = 5; player_.y = 10;  // 문 바로 안쪽
        player_.dir = 1;                  // 위 방향
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        changeScene(Scene::OVERWORLD);
    }
}

void Game::renderLabIntro() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::BLACK);

    int cx = W/2;
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    int sprY = bY - INTRO_SPR_H + 5;
    if (sprY < 1) sprY = 1;
    int halfW = INTRO_SPR_W / 2;

    // 단계별 화면:
    //   0~3: 풀숲 인터셉트 — 오박사 풀바디
    //   4:   화면 전환 (검정) — "...오박사를 따라 연구소로 갔다..."
    if (labIntroStep_ <= 3) {
        for (int i = 0; i < INTRO_SPR_H; i++)
            if (SPR_INTRO_OAK.rows[i])
                renderer.printRaw(cx - halfW, sprY + i, SPR_INTRO_OAK.rows[i]);
    }
    // step 4는 빈 화면 (전환 효과)

    // 대화창
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    const char* speaker = (labIntroStep_ == 4) ? "[ — ]" : "[OAK]";
    const char* speakerCol = (labIntroStep_ == 4) ?
        Color::BRIGHT_BLACK : Color::BRIGHT_YELLOW;
    renderer.print(bX+2,bY+1,speaker, std::string(Color::BG_BLACK)+speakerCol);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ", std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
}

void Game::renderLabIntroKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (labIntroStep_ < LAB_INTRO_COUNT)
        renderer.printW(bX+2, bY+3, LAB_INTRO_LINES[labIntroStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 포켓몬센터 ──────────────────────────────────────────────
static const wchar_t* CENTER_LINES[] = {
    L"어서오세요! 포켓몬센터입니다.",
    L"포켓몬들을 치료해드리겠습니다.",
    L"완료! 포켓몬들이 완전히 회복됐습니다!",
    nullptr
};

void Game::updateCenter(Key key) {
    if (key == Key::A) {
        if (centerStep_ == 0) {
            centerStep_++;
        } else if (centerStep_ == 1) {
            healAll(player_);
            recordBlackoutPoint(player_);
            centerStep_++;
        } else {
            if (ow_) ow_->onReturnFromCenter();
            else {
                if (!ow_) ow_ = new Overworld(renderer, player_);
                ow_->init();
            }
            changeScene(Scene::OVERWORLD);
        }
    }
}

void Game::renderCenter() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[CENTER]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_CYAN);
}

void Game::renderCenterKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (CENTER_LINES[centerStep_])
        renderer.printW(bX+2, bY+3, CENTER_LINES[centerStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 마트 이벤트 (소포 이벤트) ───────────────────────────────
static const wchar_t* MART_LINES[] = {
    L"어서오세요! 포켓몬마트입니다.",
    L"아, 혹시 팔레트시티에서 오셨나요?",
    L"오박사님께 전해드릴 소포가 있어요!",
    L"꼭 오박사님께 전해주세요!",
    nullptr
};

void Game::updateMart(Key key) {
    if (key == Key::A) {
        martStep_++;
        if (martStep_ >= 4 || !MART_LINES[martStep_]) {
            player_.gotParcel = true;
            addItem(player_, ITEM_POKE_BALL, 3);
            if (ow_) ow_->onReturnFromCenter();
            else {
                if (!ow_) ow_ = new Overworld(renderer, player_);
                ow_->init();
            }
            changeScene(Scene::OVERWORLD);
        }
    }
}

void Game::renderMart() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.print(bX+2,bY+1,"[MART]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_GREEN);
}

void Game::renderMartKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (MART_LINES[martStep_])
        renderer.printW(bX+2, bY+3, MART_LINES[martStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 마트 상점 (Scene::MART_SHOP) ────────────────────────────
void Game::updateMartShop(Key key) {
    if (shopMsgTimer_ > 0) {
        shopMsgTimer_--;
        if (shopMsgTimer_ == 0) shopMsg_ = nullptr;
    }
    const MartDef* mart = findMart(shopMartId_);
    if (!mart) {
        // 잘못된 mart — 오버월드 복귀
        if (ow_) ow_->onReturnFromCenter();
        changeScene(Scene::OVERWORLD);
        return;
    }
    int rowCount = mart->numItems + 1;  // 마지막 = 나간다

    if (shopMode_ == 0) {
        // 메뉴
        if (key == Key::UP)   shopCursor_ = (shopCursor_ - 1 + rowCount) % rowCount;
        if (key == Key::DOWN) shopCursor_ = (shopCursor_ + 1) % rowCount;
        if (key == Key::B) {
            if (ow_) ow_->onReturnFromCenter();
            changeScene(Scene::OVERWORLD);
            return;
        }
        if (key == Key::A) {
            if (shopCursor_ == mart->numItems) {
                if (ow_) ow_->onReturnFromCenter();
                changeScene(Scene::OVERWORLD);
                return;
            }
            // 아이템 구매 진입 → 수량 모드
            shopMode_     = 1;
            shopQuantity_ = 1;
        }
        return;
    }
    // shopMode_ == 1: 수량 선택
    ItemId id  = mart->items[shopCursor_];
    int    pr  = getItemPrice(id);
    int    maxByMoney = (pr > 0) ? (player_.money / pr) : 99;
    int    maxQty     = maxByMoney < 99 ? maxByMoney : 99;
    if (maxQty < 1) maxQty = 1;

    if (key == Key::UP)    shopQuantity_ = (shopQuantity_ < maxQty) ? shopQuantity_ + 1 : 1;
    if (key == Key::DOWN)  shopQuantity_ = (shopQuantity_ > 1) ? shopQuantity_ - 1 : maxQty;
    if (key == Key::RIGHT) shopQuantity_ = (shopQuantity_ + 10 <= maxQty) ? shopQuantity_ + 10 : maxQty;
    if (key == Key::LEFT)  shopQuantity_ = (shopQuantity_ - 10 >= 1) ? shopQuantity_ - 10 : 1;
    if (key == Key::B) {
        shopMode_ = 0;
        return;
    }
    if (key == Key::A) {
        int cost = pr * shopQuantity_;
        if (cost > player_.money) {
            shopMsg_      = L"돈이 부족합니다!";
            shopMsgTimer_ = 90;
        } else if (!addItem(player_, id, shopQuantity_)) {
            shopMsg_      = L"가방이 꽉 차있습니다!";
            shopMsgTimer_ = 90;
        } else {
            player_.money -= cost;
            shopMsg_      = L"감사합니다!";
            shopMsgTimer_ = 90;
        }
        shopMode_ = 0;
    }
}

void Game::renderMartShop() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0, 0, W, H, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
    int bw = 48, bh = 18, bx = W/2 - bw/2, by = H/2 - bh/2;
    renderer.drawBox(bx, by, bw, bh, std::string(Color::BG_BLACK) + Color::WHITE);
    renderer.fillRect(bx+1, by+1, bw-2, bh-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);

    const MartDef* mart = findMart(shopMartId_);
    if (!mart) return;

    char moneyBuf[32];
    snprintf(moneyBuf, sizeof(moneyBuf), "$ %d", player_.money);
    renderer.print(bx + bw - 14, by + 1, moneyBuf,
        std::string(Color::BG_BLACK) + Color::BRIGHT_GREEN);

    if (shopMode_ == 0) {
        for (int i = 0; i < mart->numItems; i++) {
            bool sel = (i == shopCursor_);
            std::string color = sel
                ? std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW
                : std::string(Color::BG_BLACK) + Color::WHITE;
            if (sel) renderer.print(bx+2, by+3+i*2, ">", color);
            char pbuf[16];
            snprintf(pbuf, sizeof(pbuf), "$ %d", getItemPrice(mart->items[i]));
            renderer.print(bx + bw - 12, by+3+i*2, pbuf,
                std::string(Color::BG_BLACK) + Color::WHITE);
        }
        bool selExit = (shopCursor_ == mart->numItems);
        std::string ec = selExit
            ? std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW
            : std::string(Color::BG_BLACK) + Color::WHITE;
        if (selExit) renderer.print(bx+2, by+3 + mart->numItems*2, ">", ec);
    } else {
        // 수량 선택: 박스 중앙에 ▲/▼ + 수량
        int pr  = getItemPrice(mart->items[shopCursor_]);
        int sum = pr * shopQuantity_;
        char qbuf[32];
        snprintf(qbuf, sizeof(qbuf), "x %2d   $ %d", shopQuantity_, sum);
        renderer.print(bx + bw/2 - 7, by + bh/2, qbuf,
            std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
    }
}

void Game::renderMartShopKorean() {
    int W = renderer.width, H = renderer.height;
    int bw = 48, bh = 18, bx = W/2 - bw/2, by = H/2 - bh/2;

    const MartDef* mart = findMart(shopMartId_);
    if (!mart) return;

    renderer.printW(bx + 2, by + 1, mart->name,
        std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);

    if (shopMode_ == 0) {
        renderer.printW(bx + 2, by + 2, L"무엇을 사시겠습니까?",
            std::string(Color::BG_BLACK) + Color::WHITE);
        for (int i = 0; i < mart->numItems; i++) {
            bool sel = (i == shopCursor_);
            std::string color = sel
                ? std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW
                : std::string(Color::BG_BLACK) + Color::WHITE;
            renderer.printW(bx + 4, by + 3 + i*2, getItemName(mart->items[i]), color);
        }
        bool selExit = (shopCursor_ == mart->numItems);
        std::string ec = selExit
            ? std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW
            : std::string(Color::BG_BLACK) + Color::WHITE;
        renderer.printW(bx + 4, by + 3 + mart->numItems*2, L"나간다", ec);
        if (shopMsg_ && shopMsgTimer_ > 0) {
            renderer.printW(bx + 2, by + bh - 4, shopMsg_,
                std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
        }
        renderer.printW(bx + 2, by + bh - 2, L"[Z]:선택 [BS]:나가기",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    } else {
        wchar_t hbuf[64];
        swprintf(hbuf, 64, L"%ls — 수량 선택", getItemName(mart->items[shopCursor_]));
        renderer.printW(bx + 2, by + 2, hbuf,
            std::string(Color::BG_BLACK) + Color::WHITE);
        renderer.printW(bx + bw/2 - 5, by + bh/2 - 2, L"▲ ▼ 1   ◄ ► 10",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        renderer.printW(bx + 2, by + bh - 2, L"[Z]:구매 [BS]:취소",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    }
}

// ─── 게임 오버 ───────────────────────────────────────────────
void Game::updateGameOver(Key key) {
    if (key == Key::A) {
        // 팔레트시티로 리스폰
        if (!ow_) ow_ = new Overworld(renderer, player_);
        ow_->init();
        changeScene(Scene::OVERWORLD);
    }
}

void Game::renderGameOver() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    int cx = W/2-5, cy = H/2;
    renderer.print(cx, cy-2, "GAME  OVER",
        std::string(Color::BG_BLACK)+Color::BRIGHT_RED);
    renderer.print(cx-2, cy+2, "[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

// ─── 엔딩 ────────────────────────────────────────────────────
static const wchar_t* ENDING_LINES[] = {
    L"브록을 쓰러뜨렸다!",
    L"바위배지를 획득했다!",
    L"이것이 모험의 첫 걸음이다!",
    L"앞으로도 더 많은 포켓몬과 만남이 기다린다.",
    L"【 클리어! 플레이 감사합니다! 】",
    nullptr
};

void Game::updateEnding(Key key) {
    if (key == Key::A) {
        endingStep_++;
        if (!ENDING_LINES[endingStep_]) {
            running = false; // 게임 종료
        }
    }
}

void Game::renderEnding() {
    int W = renderer.width, H = renderer.height;
    renderer.fillRect(0,0,W,H,' ', std::string(Color::BG_BLACK)+Color::WHITE);
    // 배지 표시
    int cx = W/2;
    renderer.print(cx-6, H/2-4, " *** BADGE *** ",
        std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
    renderer.print(cx-5, H/2-3, "  [BOULDER]  ",
        std::string(Color::BG_BLACK)+Color::YELLOW);
    int bH=6, bY=H-bH-1, bW=W-4, bX=2;
    renderer.drawBox(bX,bY,bW,bH, std::string(Color::BG_BLACK)+Color::WHITE);
    renderer.fillRect(bX+1,bY+1,bW-2,bH-2,' ',std::string(Color::BG_BLACK)+Color::WHITE);
    if ((frame_/8)%2==0)
        renderer.print(bX+bW-4,bY+bH-2," v ",
            std::string(Color::BG_BLACK)+Color::BRIGHT_YELLOW);
}

void Game::renderEndingKorean() {
    int H = renderer.height;
    int bH=6, bY=H-bH-1, bX=2;
    if (ENDING_LINES[endingStep_])
        renderer.printW(bX+2, bY+3, ENDING_LINES[endingStep_],
            std::string(Color::BG_BLACK)+Color::BRIGHT_WHITE);
    renderer.printW(bX+2, bY+bH-2, L"[ Z: 계속 ]",
        std::string(Color::BG_BLACK)+Color::BRIGHT_BLACK);
}

void Game::startDialog(const wchar_t** lines, int count, Scene next) {
    for (int i = 0; i < count && i < 6; i++) dialogLines_[i] = lines[i];
    dialogCount_ = count;
    dialogStep_  = 0;
    dialogNext_  = next;
}

// ─── 인게임 메뉴 (M 키) ──────────────────────────────────────
static const char* menuHpColor(int cur, int mx) {
    if (mx <= 0) return Color::BRIGHT_BLACK;
    int pct = cur * 100 / mx;
    if (pct > 50) return Color::BRIGHT_GREEN;
    if (pct > 25) return Color::BRIGHT_YELLOW;
    return Color::BRIGHT_RED;
}

static void menuHpBar(char* out, int cur, int mx, int len) {
    int filled = (mx > 0) ? (cur * len / mx) : 0;
    out[0] = '[';
    for (int i = 0; i < len; i++) out[1+i] = (i < filled) ? '#' : '-';
    out[1+len] = ']';
    out[2+len] = '\0';
}

static const wchar_t* menuTypeName(Type t) {
    switch (t) {
    case Type::NORMAL:   return L"노말";
    case Type::FIRE:     return L"불꽃";
    case Type::WATER:    return L"물";
    case Type::GRASS:    return L"풀";
    case Type::ELECTRIC: return L"전기";
    case Type::BUG:      return L"벌레";
    case Type::ROCK:     return L"바위";
    case Type::GROUND:   return L"땅";
    case Type::POISON:   return L"독";
    case Type::FLYING:   return L"비행";
    case Type::PSYCHIC:  return L"에스퍼";
    case Type::GHOST:    return L"고스트";
    default:             return L"-";
    }
}

void Game::updateInGameMenu(Key key) {
    // 메뉴 조작 효과음
    if (key == Key::UP || key == Key::DOWN || key == Key::LEFT || key == Key::RIGHT)
        Audio::playSE("se_cursor");
    else if (key == Key::A) Audio::playSE("se_select");
    else if (key == Key::B) Audio::playSE("se_back");

    if (key == Key::B) {
        if (menuState_ == InGameMenuState::TOP_LEVEL) {
            changeScene(prevScene_);
            return;
        } else if (menuState_ == InGameMenuState::ITEM_TARGET) {
            menuState_ = InGameMenuState::ITEM_BAG;
            return;
        } else if (menuState_ == InGameMenuState::PARTY_DETAIL) {
            // 기술 선택 중이면 해제, 아니면 파티 목록으로
            if (detailSwapSel_ >= 0) {
                detailSwapSel_ = -1;
            } else {
                menuState_ = InGameMenuState::PARTY_VIEW;
            }
            return;
        } else if (menuState_ == InGameMenuState::POKEDEX_DETAIL) {
            menuState_ = InGameMenuState::POKEDEX;
            return;
        } else if (menuState_ == InGameMenuState::ITEM_MESSAGE ||
                   menuState_ == InGameMenuState::ITEM_LEARN_SELECT) {
            // ITEM_MESSAGE / ITEM_LEARN_SELECT는 아래 switch에서 자체 처리
            // (TOP_LEVEL로 점프하지 않도록 여기서 return 안 함)
        } else {
            menuState_  = InGameMenuState::TOP_LEVEL;
            menuCursor_ = 0;
            return;
        }
    }

    switch (menuState_) {
    case InGameMenuState::TOP_LEVEL:
        // 원작 시작 메뉴 6항목: 0포켓덱스 1포켓몬 2가방 3이름 4설정 5닫기
        if (itemMsgTimer_ > 0) { itemMsgTimer_--; if (itemMsgTimer_ == 0) itemMsg_ = nullptr; }
        if (key == Key::UP)   menuCursor_ = (menuCursor_ + 5) % 6;
        if (key == Key::DOWN) menuCursor_ = (menuCursor_ + 1) % 6;
        if (key == Key::A) {
            switch (menuCursor_) {
            case 0:
                menuState_ = InGameMenuState::POKEDEX;
                dexCursor_ = 0;
                break;
            case 1:
                menuState_ = InGameMenuState::PARTY_VIEW;
                partyMenuCursor_ = 0;
                break;
            case 2:
                menuState_ = InGameMenuState::ITEM_BAG;
                itemMenuCursor_ = 0;
                itemMsg_ = nullptr;
                itemMsgTimer_ = 0;
                break;
            case 3:  // 이름(트레이너 카드)
                swprintf(itemMsgBuf_, 64, L"%ls   소지금: $%d", player_.name, player_.money);
                itemMsg_ = itemMsgBuf_;
                itemMsgTimer_ = 120;
                break;
            case 4:  // 설정
                itemMsg_ = L"설정은 준비 중입니다.";
                itemMsgTimer_ = 120;
                break;
            case 5:  // 닫기
                changeScene(prevScene_);
                break;
            }
        }
        break;
    case InGameMenuState::PARTY_VIEW:
        if (key == Key::UP)   partyMenuCursor_ = (partyMenuCursor_ + player_.partySize - 1) % player_.partySize;
        if (key == Key::DOWN) partyMenuCursor_ = (partyMenuCursor_ + 1) % player_.partySize;
        if (key == Key::A && player_.partySize > 0) {
            detailPartyIdx_   = partyMenuCursor_;
            detailMoveCursor_ = 0;
            detailSwapSel_    = -1;
            menuState_ = InGameMenuState::PARTY_DETAIL;
        }
        break;
    case InGameMenuState::PARTY_DETAIL: {
        // 커서: 0~numMoves-1 = 기술 슬롯, numMoves = "선두로 지정"
        Pokemon& dp = player_.party[detailPartyIdx_];
        int nOpt = dp.numMoves + 1;
        if (key == Key::UP)   detailMoveCursor_ = (detailMoveCursor_ - 1 + nOpt) % nOpt;
        if (key == Key::DOWN) detailMoveCursor_ = (detailMoveCursor_ + 1) % nOpt;
        if (key == Key::A) {
            if (detailMoveCursor_ < dp.numMoves) {
                // 기술 슬롯 — 순서 교체 (첫 선택 → 두 번째 선택 시 swap)
                if (detailSwapSel_ < 0) {
                    detailSwapSel_ = detailMoveCursor_;
                } else if (detailSwapSel_ == detailMoveCursor_) {
                    detailSwapSel_ = -1;  // 같은 슬롯 → 선택 해제
                } else {
                    PokemonMove tmp = dp.moves[detailSwapSel_];
                    dp.moves[detailSwapSel_]   = dp.moves[detailMoveCursor_];
                    dp.moves[detailMoveCursor_] = tmp;
                    detailSwapSel_ = -1;
                }
            } else {
                // "선두로 지정" — 0번과 교체
                if (detailPartyIdx_ > 0 && detailPartyIdx_ < player_.partySize) {
                    Pokemon tmp = player_.party[0];
                    player_.party[0] = player_.party[detailPartyIdx_];
                    player_.party[detailPartyIdx_] = tmp;
                }
                partyMenuCursor_ = 0;
                detailSwapSel_   = -1;
                menuState_ = InGameMenuState::PARTY_VIEW;
            }
        }
        break;
    }
    case InGameMenuState::ITEM_BAG: {
        if (itemMsgTimer_ > 0) {
            itemMsgTimer_--;
            if (itemMsgTimer_ == 0) itemMsg_ = nullptr;
        }
        int n = player_.bagSize;
        if (n == 0) {
            // 가방 비어있음 — 키 입력 시 위 메뉴로 복귀 (커서 = 가방)
            if (key == Key::A || key == Key::B) {
                menuState_  = InGameMenuState::TOP_LEVEL;
                menuCursor_ = 2;
            }
            break;
        }
        if (key == Key::UP)   itemMenuCursor_ = (itemMenuCursor_ - 1 + n) % n;
        if (key == Key::DOWN) itemMenuCursor_ = (itemMenuCursor_ + 1) % n;
        if (key == Key::A) {
            ItemId id = player_.bag[itemMenuCursor_].id;
            if (id == ITEM_POTION) {
                // 회복 대상 파티 선택
                if (player_.partySize == 0) {
                    itemMsg_ = L"포켓몬이 없다!";
                    itemMsgTimer_ = 60;
                } else {
                    menuState_ = InGameMenuState::ITEM_TARGET;
                    itemTargetCursor_ = 0;
                }
            } else if (id == ITEM_RARE_CANDY) {
                // 이상한사탕 — 레벨 올릴 대상 파티 선택
                if (player_.partySize == 0) {
                    itemMsg_ = L"포켓몬이 없다!";
                    itemMsgTimer_ = 60;
                } else {
                    menuState_ = InGameMenuState::ITEM_TARGET;
                    itemTargetCursor_ = 0;
                }
            } else if (id == ITEM_POKE_BALL) {
                itemMsg_ = L"이건 배틀 중에만 쓸 수 있다!";
                itemMsgTimer_ = 90;
            }
        }
        break;
    }
    case InGameMenuState::ITEM_TARGET: {
        int n = player_.partySize;
        if (n == 0) {
            menuState_ = InGameMenuState::ITEM_BAG;
            break;
        }
        if (key == Key::UP)   itemTargetCursor_ = (itemTargetCursor_ - 1 + n) % n;
        if (key == Key::DOWN) itemTargetCursor_ = (itemTargetCursor_ + 1) % n;
        if (key == Key::A) {
            ItemId useId = (itemMenuCursor_ < player_.bagSize)
                ? player_.bag[itemMenuCursor_].id : ITEM_NONE;
            Pokemon& p = player_.party[itemTargetCursor_];
            if (!p.species) {
                itemMsg_ = L"빈 슬롯이다!";
                itemMsgTimer_ = 60;
            } else if (useId == ITEM_RARE_CANDY) {
                // 이상한사탕 — 레벨 +1, 3의 배수면 새 기술 학습 또는 잊을 기술 선택 UI
                if (p.level >= 100) {
                    itemMsg_ = L"더 이상 레벨을 올릴 수 없다!";
                    itemMsgTimer_ = 90;
                    menuState_ = InGameMenuState::ITEM_BAG;
                } else {
                    int wantedMid = 0;
                    int learnedMid = rareCandyLevelUp(p, &wantedMid);
                    removeItem(player_, ITEM_RARE_CANDY, 1);
                    // 1줄: 레벨업
                    swprintf(itemMsgBuf_, 128, L"이상한사탕! %ls의 레벨이 %d(으)로 올랐다!",
                        p.species->name, p.level);
                    itemMsg_ = itemMsgBuf_;
                    // 2줄: 빈슬롯=배웠다, 4가득=배우고 싶어한다 (이후 잊을 기술 선택)
                    if (learnedMid > 0) {
                        swprintf(itemMsgBuf2_, 128, L"%ls은(는) 새로운 기술 %ls을(를) 배웠다!",
                            p.species->name, getMoveData(learnedMid).name);
                        itemMsg2_ = itemMsgBuf2_;
                        pendingLearnNewMid_ = 0;
                    } else if (wantedMid > 0) {
                        swprintf(itemMsgBuf2_, 128, L"%ls은(는) 새로운 기술 %ls을(를) 배우고 싶어한다!",
                            p.species->name, getMoveData(wantedMid).name);
                        itemMsg2_ = itemMsgBuf2_;
                        pendingLearnTargetIdx_ = itemTargetCursor_;
                        pendingLearnNewMid_    = wantedMid;
                        pendingLearnCursor_    = 0;
                    } else {
                        itemMsgBuf2_[0] = 0;
                        itemMsg2_ = nullptr;
                        pendingLearnNewMid_ = 0;
                    }
                    itemMsgTimer_ = 0;
                    menuState_ = InGameMenuState::ITEM_MESSAGE;
                    if (itemMenuCursor_ >= player_.bagSize && player_.bagSize > 0)
                        itemMenuCursor_ = player_.bagSize - 1;
                }
            } else if (p.currentHP <= 0) {
                itemMsg_ = L"쓰러진 포켓몬에겐 효과가 없다!";
                itemMsgTimer_ = 90;
                menuState_ = InGameMenuState::ITEM_BAG;
            } else if (p.currentHP >= p.maxHP) {
                itemMsg_ = L"HP가 가득 차 있다!";
                itemMsgTimer_ = 90;
                menuState_ = InGameMenuState::ITEM_BAG;
            } else {
                int heal = POTION_HEAL_AMOUNT;
                if (p.currentHP + heal > p.maxHP) heal = p.maxHP - p.currentHP;
                p.currentHP += heal;
                removeItem(player_, ITEM_POTION, 1);
                itemMsg_ = L"상처약을 사용했다!";
                itemMsgTimer_ = 90;
                menuState_ = InGameMenuState::ITEM_BAG;
                // 가방이 비거나 커서 범위 벗어나면 보정
                if (player_.bagSize == 0) {
                    menuState_  = InGameMenuState::TOP_LEVEL;
                    menuCursor_ = 2;
                } else if (itemMenuCursor_ >= player_.bagSize) {
                    itemMenuCursor_ = player_.bagSize - 1;
                }
            }
        }
        if (key == Key::B) {
            menuState_ = InGameMenuState::ITEM_BAG;
        }
        break;
    }
    case InGameMenuState::ITEM_MESSAGE: {
        // 아이템 사용 결과 대화상자 — A/Z/Enter/B 입력 시 다음 단계로
        if (key == Key::A || key == Key::B) {
            if (pendingLearnNewMid_ > 0) {
                // 4가득 — 잊을 기술 선택 UI로 전환
                pendingLearnCursor_ = 0;
                menuState_ = InGameMenuState::ITEM_LEARN_SELECT;
            } else {
                // 메시지 정리 후 가방으로 복귀
                itemMsg_      = nullptr;
                itemMsg2_     = nullptr;
                itemMsgTimer_ = 0;
                if (player_.bagSize == 0) {
                    menuState_  = InGameMenuState::TOP_LEVEL;
                    menuCursor_ = 2;
                } else {
                    menuState_ = InGameMenuState::ITEM_BAG;
                }
            }
        }
        break;
    }
    case InGameMenuState::ITEM_LEARN_SELECT: {
        // 잊을 기술 선택 — 0~numMoves-1 = 기술, numMoves = 배우지 않기
        if (pendingLearnTargetIdx_ < 0 || pendingLearnTargetIdx_ >= player_.partySize) {
            // 안전 가드 — 잘못된 상태면 가방으로 복귀
            pendingLearnNewMid_ = 0;
            menuState_ = InGameMenuState::ITEM_BAG;
            break;
        }
        Pokemon& target = player_.party[pendingLearnTargetIdx_];
        int nOpt = target.numMoves + 1;  // +1 = 배우지 않기
        if (key == Key::UP)
            pendingLearnCursor_ = (pendingLearnCursor_ - 1 + nOpt) % nOpt;
        if (key == Key::DOWN)
            pendingLearnCursor_ = (pendingLearnCursor_ + 1) % nOpt;
        // B 또는 (A + "배우지 않기" 위치) → 학습 포기
        if (key == Key::B || (key == Key::A && pendingLearnCursor_ >= target.numMoves)) {
            swprintf(itemMsgBuf_, 128, L"%ls은(는) %ls을(를) 배우지 않았다.",
                target.species->name, getMoveData(pendingLearnNewMid_).name);
            itemMsg_ = itemMsgBuf_;
            itemMsgBuf2_[0] = 0;
            itemMsg2_ = nullptr;
            pendingLearnNewMid_ = 0;
            menuState_ = InGameMenuState::ITEM_MESSAGE;
        } else if (key == Key::A) {
            // 선택한 슬롯의 기술을 새 기술로 교체
            int slot  = pendingLearnCursor_;
            int oldId = target.moves[slot].moveId;
            int newId = pendingLearnNewMid_;
            target.moves[slot].moveId = newId;
            target.moves[slot].pp     = getMoveData(newId).maxPP;
            swprintf(itemMsgBuf_, 128, L"%ls은(는) %ls을(를) 잊고",
                target.species->name, getMoveData(oldId).name);
            swprintf(itemMsgBuf2_, 128, L"새로운 기술 %ls을(를) 배웠다!",
                getMoveData(newId).name);
            itemMsg_  = itemMsgBuf_;
            itemMsg2_ = itemMsgBuf2_;
            pendingLearnNewMid_ = 0;
            menuState_ = InGameMenuState::ITEM_MESSAGE;
        }
        break;
    }
    case InGameMenuState::POKEDEX: {
        int n = NUM_SPECIES_DATA;
        if (key == Key::UP)   dexCursor_ = (dexCursor_ - 1 + n) % n;
        if (key == Key::DOWN) dexCursor_ = (dexCursor_ + 1) % n;
        if (key == Key::A) {
            // 포획/처치한(등록된) 포켓몬만 상세 진입
            int di = dexCursor_;
            if (player_.dexCaught[di] > 0 || player_.dexDefeated[di] > 0) {
                dexDetailIdx_ = di;
                menuState_ = InGameMenuState::POKEDEX_DETAIL;
            }
        }
        break;
    }
    case InGameMenuState::POKEDEX_DETAIL:
        if (key == Key::A) menuState_ = InGameMenuState::POKEDEX;
        break;
    }
}

void Game::renderInGameMenu() {
    int W = renderer.width, H = renderer.height;
    // 원본처럼: 화면을 검정으로 덮지 않고, 메뉴를 연 시점의 오버월드를 뒤에 그린다.
    // (두 렌더 패스는 같은 셀 버퍼를 공유하므로 여기서 맵을 먼저 그리면
    //  아래의 메뉴 박스가 그 위에 불투명하게 덮인다.)
    if (ow_) {
        ow_->render();
        ow_->renderKorean();
    } else {
        renderer.fillRect(0, 0, W, H, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
    }

    if (menuState_ == InGameMenuState::TOP_LEVEL) {
        // 원작 RBY 시작 메뉴: 흰 박스 + 검은 글씨, 우측 배치, 6항목
        int mw = 20, mh = 10, mx = W - mw - 2, my = 2;
        std::string wb = std::string(Color::BG_WHITE) + Color::BLACK;
        renderer.fillRect(mx+1, my+1, mw-2, mh-2, ' ', wb);
        renderer.drawBox(mx, my, mw, mh, wb);
        // 커서(>) — 항목 텍스트는 한글 레이어에서 그림
        for (int i = 0; i < 6; i++) {
            if (i == menuCursor_)
                renderer.print(mx+2, my+2+i, ">", wb);
        }
    } else if (menuState_ == InGameMenuState::POKEDEX) {
        int bw = 50, bh = NUM_SPECIES_DATA + 6, bx = W/2 - bw/2, by = 2;
        renderer.drawBox(bx, by, bw, bh, std::string(Color::BG_BLACK) + Color::WHITE);
        renderer.fillRect(bx+1, by+1, bw-2, bh-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        for (int i = 0; i < NUM_SPECIES_DATA; i++) {
            bool isCur = (i == dexCursor_);
            if (isCur) renderer.print(bx+2, by+3+i, ">",
                std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
            char numbuf[16];
            snprintf(numbuf, sizeof(numbuf), "No.%03d", SPECIES[i].id);
            renderer.print(bx+4, by+3+i, numbuf, std::string(Color::BG_BLACK) +
                (isCur ? Color::BRIGHT_YELLOW : Color::WHITE));
        }
    } else if (menuState_ == InGameMenuState::POKEDEX_DETAIL) {
        int bw = 70, bh = 24, bx = W/2 - bw/2, by = 2;
        renderer.drawBox(bx, by, bw, bh, std::string(Color::BG_BLACK) + Color::WHITE);
        renderer.fillRect(bx+1, by+1, bw-2, bh-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        const PokemonSpecies& sp = SPECIES[dexDetailIdx_];
        // 스프라이트 (왼쪽) — 40폭 × 최대 20행
        const SpriteData* spr = getSpriteFront(sp.id);
        if (spr) {
            for (int r = 0; r < spr->height && spr->rows[r]; r++)
                renderer.printRaw(bx+3, by+2+r, spr->rows[r]);
        }
        int tx = bx + 46;   // 오른쪽 텍스트 열
        char numbuf[16];
        snprintf(numbuf, sizeof(numbuf), "No.%03d", sp.id);
        renderer.print(tx, by+2, numbuf, std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        char cntbuf[32];
        snprintf(cntbuf, sizeof(cntbuf), "%d / %d",
            player_.dexCaught[dexDetailIdx_], player_.dexDefeated[dexDetailIdx_]);
        renderer.print(tx, by+15, cntbuf, std::string(Color::BG_BLACK) + Color::WHITE);
    } else if (menuState_ == InGameMenuState::PARTY_VIEW) {
        int bw = 66, bh = 15, bx = W/2 - 33, by = 3;
        renderer.drawBox(bx, by, bw, bh, std::string(Color::BG_BLACK) + Color::WHITE);
        renderer.fillRect(bx+1, by+1, bw-2, bh-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        for (int i = 0; i < player_.partySize; i++) {
            const Pokemon& p = player_.party[i];
            if (!p.species) continue;
            std::string color = (i == partyMenuCursor_)
                ? std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW
                : std::string(Color::BG_BLACK) + Color::WHITE;
            if (i == partyMenuCursor_)
                renderer.print(bx+2, by+3+i, ">", color);
            char lvbuf[16];
            snprintf(lvbuf, sizeof(lvbuf), "Lv.%-3d", p.level);
            renderer.print(bx+18, by+3+i, lvbuf, std::string(Color::BG_BLACK) + Color::WHITE);
            char hpbar[16];
            menuHpBar(hpbar, p.currentHP, p.maxHP, 10);
            renderer.print(bx+25, by+3+i, hpbar,
                std::string(Color::BG_BLACK) + menuHpColor(p.currentHP, p.maxHP));
            char hpnum[16];
            snprintf(hpnum, sizeof(hpnum), " %3d/%3d", p.currentHP, p.maxHP);
            renderer.print(bx+37, by+3+i, hpnum, std::string(Color::BG_BLACK) + Color::WHITE);
        }
    } else if (menuState_ == InGameMenuState::PARTY_DETAIL) {
        int bw = 62, bh = 24, bx = W/2 - bw/2, by = 2;
        renderer.drawBox(bx, by, bw, bh, std::string(Color::BG_BLACK) + Color::WHITE);
        renderer.fillRect(bx+1, by+1, bw-2, bh-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        const Pokemon& p = player_.party[detailPartyIdx_];
        if (p.species) {
            char statbuf[64];
            snprintf(statbuf, sizeof(statbuf), "Lv.%-3d  %d/%d", p.level, p.currentHP, p.maxHP);
            renderer.print(bx+8, by+5, statbuf, std::string(Color::BG_BLACK) + Color::WHITE);
            snprintf(statbuf, sizeof(statbuf), "%-4d", p.atk);
            renderer.print(bx+9, by+7, statbuf, std::string(Color::BG_BLACK) + Color::WHITE);
            snprintf(statbuf, sizeof(statbuf), "%-4d", p.def);
            renderer.print(bx+21, by+7, statbuf, std::string(Color::BG_BLACK) + Color::WHITE);
            snprintf(statbuf, sizeof(statbuf), "%-4d", p.spe);
            renderer.print(bx+9, by+8, statbuf, std::string(Color::BG_BLACK) + Color::WHITE);
            snprintf(statbuf, sizeof(statbuf), "%-4d", p.spc);
            renderer.print(bx+21, by+8, statbuf, std::string(Color::BG_BLACK) + Color::WHITE);
            // 경험치 바 (현재 레벨 구간 진행도)
            int curE  = p.exp - expForLevel(p.level);
            int needE = expForLevel(p.level + 1) - expForLevel(p.level);
            if (needE < 1) needE = 1;
            if (curE  < 0) curE  = 0;
            char ebar[16];
            menuHpBar(ebar, curE, needE, 10);
            renderer.print(bx+12, by+9, ebar,
                std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
            char ebuf[32];
            snprintf(ebuf, sizeof(ebuf), "%d/%d", curE, needE);
            renderer.print(bx+24, by+9, ebuf, std::string(Color::BG_BLACK) + Color::WHITE);
            for (int i = 0; i < p.numMoves; i++) {
                const MoveData& mv = getMoveData(p.moves[i].moveId);
                char mvbuf[64];
                if (mv.power > 0)
                    snprintf(mvbuf, sizeof(mvbuf), "PP:%2d/%2d  PWR:%3d  ACC:%3d",
                        p.moves[i].pp, mv.maxPP, mv.power, mv.accuracy);
                else
                    snprintf(mvbuf, sizeof(mvbuf), "PP:%2d/%2d  PWR: --  ACC:%3d",
                        p.moves[i].pp, mv.maxPP, mv.accuracy);
                renderer.print(bx+28, by+11+i, mvbuf, std::string(Color::BG_BLACK) + Color::WHITE);
            }
        }
    } else if (menuState_ == InGameMenuState::ITEM_BAG ||
               menuState_ == InGameMenuState::ITEM_TARGET ||
               menuState_ == InGameMenuState::ITEM_MESSAGE ||
               menuState_ == InGameMenuState::ITEM_LEARN_SELECT) {
        int bw = 44, bh = 14, bx = W/2 - 22, by = 5;
        renderer.drawBox(bx, by, bw, bh, std::string(Color::BG_BLACK) + Color::WHITE);
        renderer.fillRect(bx+1, by+1, bw-2, bh-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        // 가방 아이템 리스트 (커서 표시)
        for (int i = 0; i < player_.bagSize; i++) {
            bool sel = (menuState_ == InGameMenuState::ITEM_BAG && i == itemMenuCursor_);
            if (sel) renderer.print(bx+2, by+3+i, ">",
                std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
            char cntBuf[16];
            snprintf(cntBuf, sizeof(cntBuf), "x%-2d", player_.bag[i].count);
            renderer.print(bx+bw-8, by+3+i, cntBuf,
                std::string(Color::BG_BLACK) + Color::WHITE);
        }
        // 돈 표시
        char moneyBuf[32];
        snprintf(moneyBuf, sizeof(moneyBuf), "$%d", player_.money);
        renderer.print(bx+bw-12, by+1, moneyBuf,
            std::string(Color::BG_BLACK) + Color::BRIGHT_GREEN);
    }
}

void Game::renderInGameMenuKorean() {
    int W = renderer.width;

    if (menuState_ == InGameMenuState::TOP_LEVEL) {
        // 흰 박스 + 검은 글씨, 원작 7항목 (이름 항목엔 플레이어 이름)
        int mw = 20, mx = W - mw - 2, my = 2;
        std::string wb = std::string(Color::BG_WHITE) + Color::BLACK;
        const wchar_t* menuLabels[6] = {
            L"포켓덱스", L"포켓몬", L"가방", player_.name, L"설정", L"닫기"
        };
        for (int i = 0; i < 6; i++)
            renderer.printW(mx + 4, my + 2 + i, menuLabels[i], wb);
        // 이름/리포트/설정 메시지 (화면 하단)
        if (itemMsg_ && itemMsgTimer_ > 0)
            renderer.printW(2, renderer.height - 2, itemMsg_,
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
    } else if (menuState_ == InGameMenuState::POKEDEX) {
        int bw = 50, bh = NUM_SPECIES_DATA + 6, bx = W/2 - bw/2, by = 2;
        renderer.printW(bx + bw/2 - 4, by + 1, L"포켓덱스",
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        for (int i = 0; i < NUM_SPECIES_DATA; i++) {
            bool seen = (player_.dexCaught[i] > 0 || player_.dexDefeated[i] > 0);
            bool isCur = (i == dexCursor_);
            std::string color = std::string(Color::BG_BLACK) +
                (isCur ? Color::BRIGHT_YELLOW : (seen ? Color::WHITE : Color::BRIGHT_BLACK));
            // ASCII 레이어가 "No.###"(bx+4)을 그림. 이름은 bx+14에.
            renderer.printW(bx + 14, by + 3 + i, seen ? SPECIES[i].name : L"???", color);
        }
        renderer.printW(bx + 2, by + bh - 2, L"[Z] 상세  [BS] 뒤로",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    } else if (menuState_ == InGameMenuState::POKEDEX_DETAIL) {
        int bw = 70, bh = 24, bx = W/2 - bw/2, by = 2;
        int tx = bx + 46;   // 오른쪽 텍스트 열 (왼쪽은 스프라이트)
        const PokemonSpecies& sp = SPECIES[dexDetailIdx_];
        // 이름 (도감번호는 ASCII 레이어 위에)
        renderer.printW(tx, by + 3, sp.name,
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        // 속성
        wchar_t typeBuf[32];
        if (sp.type2 != Type::NONE)
            swprintf(typeBuf, 32, L"속성: %ls / %ls", menuTypeName(sp.type1), menuTypeName(sp.type2));
        else
            swprintf(typeBuf, 32, L"속성: %ls", menuTypeName(sp.type1));
        renderer.printW(tx, by + 5, typeBuf, std::string(Color::BG_BLACK) + Color::CYAN);
        // 기본 기술
        renderer.printW(tx, by + 7, L"기본 기술",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        int row = 0;
        for (int i = 0; i < 4; i++) {
            int mid = sp.startMoves[i];
            if (mid == 0) continue;
            const MoveData& mv = getMoveData(mid);
            renderer.printW(tx + 1, by + 8 + row, mv.name,
                std::string(Color::BG_BLACK) + Color::WHITE);
            row++;
        }
        // 포획/처치 수 라벨 (수치는 ASCII 레이어)
        renderer.printW(tx, by + 14, L"포획 / 처치",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        renderer.printW(bx + 2, by + bh - 2, L"[BS] 뒤로",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    } else if (menuState_ == InGameMenuState::PARTY_VIEW) {
        int bw = 66, bh = 15, bx = W/2 - 33, by = 3;
        renderer.printW(bx + bw/2 - 5, by + 1, L"포켓몬 파티",
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        for (int i = 0; i < player_.partySize; i++) {
            const Pokemon& p = player_.party[i];
            if (!p.species) continue;
            std::string color = (i == partyMenuCursor_)
                ? std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW
                : std::string(Color::BG_BLACK) + Color::WHITE;
            renderer.printW(bx + 4, by + 3 + i, p.species->name, color);
            // Lv·HP바·HP숫자는 ASCII 레이어(renderInGameMenu)에서 그림.
            // 선두 표시는 HP 숫자(bx+37~bx+44) 오른쪽에 배치해 체력 바를 가리지 않게 함.
            if (i == 0)
                renderer.printW(bx + 47, by + 3 + i, L"◀선두",
                    std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
        }
        renderer.printW(bx + 2, by + bh - 2, L"[Z]: 상세/선두지정  [BS]: 뒤로",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    } else if (menuState_ == InGameMenuState::PARTY_DETAIL) {
        int bw = 62, bh = 24, bx = W/2 - bw/2, by = 2;
        const Pokemon& p = player_.party[detailPartyIdx_];
        if (p.species) {
            renderer.printW(bx + bw/2 - 6, by + 1, L"[ 포켓몬 상세 ]",
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
            renderer.printW(bx + 4, by + 2, p.species->name,
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
            // 타입 (단일/이중)
            wchar_t typeBuf[32];
            if (p.species->type2 != Type::NONE)
                swprintf(typeBuf, 32, L"%ls / %ls",
                    menuTypeName(p.species->type1), menuTypeName(p.species->type2));
            else
                swprintf(typeBuf, 32, L"%ls", menuTypeName(p.species->type1));
            renderer.printW(bx + 4, by + 3, typeBuf,
                std::string(Color::BG_BLACK) + Color::CYAN);
            // 스탯 라벨 (ASCII 수치 위에 겹침)
            renderer.printW(bx + 4,  by + 5, L"HP",   std::string(Color::BG_BLACK) + Color::WHITE);
            renderer.printW(bx + 4,  by + 7, L"공격", std::string(Color::BG_BLACK) + Color::WHITE);
            renderer.printW(bx + 16, by + 7, L"방어", std::string(Color::BG_BLACK) + Color::WHITE);
            renderer.printW(bx + 4,  by + 8, L"속도", std::string(Color::BG_BLACK) + Color::WHITE);
            renderer.printW(bx + 16, by + 8, L"특수", std::string(Color::BG_BLACK) + Color::WHITE);
            // 경험치 라벨 + 레벨업까지 남은 경험치 (바/수치는 ASCII 레이어가 그림)
            renderer.printW(bx + 4, by + 9, L"경험치", std::string(Color::BG_BLACK) + Color::WHITE);
            {
                int toNext = expForLevel(p.level + 1) - p.exp;
                if (toNext < 0) toNext = 0;
                wchar_t nbuf[48];
                swprintf(nbuf, 48, L"다음 레벨까지 %d", toNext);
                renderer.printW(bx + 38, by + 9, nbuf,
                    std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
            }
            // 기술 섹션 — 커서/순서 교체 선택 표시
            renderer.printW(bx + 4, by + 10, L"── 기술 (순서 변경 가능) ──",
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
            for (int i = 0; i < p.numMoves; i++) {
                const MoveData& mv = getMoveData(p.moves[i].moveId);
                bool isCur = (detailMoveCursor_ == i);
                bool isSel = (detailSwapSel_ == i);
                std::string mc = std::string(Color::BG_BLACK) +
                    (isSel ? Color::BRIGHT_GREEN : (isCur ? Color::BRIGHT_YELLOW : Color::WHITE));
                if (isCur) renderer.print(bx + 2, by + 11 + i, ">", mc);
                wchar_t slotbuf[40];
                swprintf(slotbuf, 40, L"%d. %ls", i + 1, mv.name);
                renderer.printW(bx + 4, by + 11 + i, slotbuf, mc);
                renderer.printW(bx + 20, by + 11 + i, menuTypeName(mv.type),
                    std::string(Color::BG_BLACK) + Color::CYAN);
            }
            // "선두로 지정" 액션 항목
            {
                bool isCur = (detailMoveCursor_ == p.numMoves);
                std::string mc = std::string(Color::BG_BLACK) +
                    (isCur ? Color::BRIGHT_YELLOW : Color::BRIGHT_CYAN);
                if (isCur) renderer.print(bx + 2, by + 11 + p.numMoves, ">", mc);
                renderer.printW(bx + 4, by + 11 + p.numMoves, L"▶ 이 포켓몬을 선두로", mc);
            }
            // 상성 섹션
            renderer.printW(bx + 4, by + 16, L"── 상성 ──",
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
            Type def1 = p.species->type1;
            Type def2 = p.species->type2;
            wchar_t weakBuf[128]   = L"";
            wchar_t resistBuf[128] = L"";
            wchar_t immuneBuf[128] = L"";
            for (int ti = 0; ti < 12; ti++) {
                Type atk = (Type)ti;
                int eff = getEffInt(atk, def1, def2);
                if (eff == 0) {
                    if (wcslen(immuneBuf) > 0) wcscat(immuneBuf, L" ");
                    wcscat(immuneBuf, menuTypeName(atk));
                } else if (eff >= 20) {
                    if (wcslen(weakBuf) > 0) wcscat(weakBuf, L" ");
                    wcscat(weakBuf, menuTypeName(atk));
                } else if (eff <= 5) {
                    if (wcslen(resistBuf) > 0) wcscat(resistBuf, L" ");
                    wcscat(resistBuf, menuTypeName(atk));
                }
            }
            if (wcslen(weakBuf)   == 0) wcscpy(weakBuf,   L"없음");
            if (wcslen(resistBuf) == 0) wcscpy(resistBuf, L"없음");
            if (wcslen(immuneBuf) == 0) wcscpy(immuneBuf, L"없음");
            wchar_t lineBuf[128];
            swprintf(lineBuf, 128, L"약점(2x) : %ls", weakBuf);
            renderer.printW(bx + 4, by + 17, lineBuf,
                std::string(Color::BG_BLACK) + Color::BRIGHT_RED);
            swprintf(lineBuf, 128, L"저항(½x) : %ls", resistBuf);
            renderer.printW(bx + 4, by + 18, lineBuf,
                std::string(Color::BG_BLACK) + Color::BRIGHT_GREEN);
            swprintf(lineBuf, 128, L"무효(0x) : %ls", immuneBuf);
            renderer.printW(bx + 4, by + 19, lineBuf,
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        }
        renderer.printW(bx + 2, by + bh - 2, L"[↑↓] 이동  [Z] 선택→교체/선두지정  [BS] 뒤로",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    } else if (menuState_ == InGameMenuState::ITEM_BAG) {
        int bw = 44, bh = 14, bx = W/2 - 22, by = 5;
        renderer.printW(bx + bw/2 - 2, by + 1, L"가방",
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        if (player_.bagSize == 0) {
            renderer.printW(bx + 4, by + 4, L"가방이 비어있다.",
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        } else {
            for (int i = 0; i < player_.bagSize; i++) {
                bool sel = (i == itemMenuCursor_);
                std::string color = sel
                    ? std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW
                    : std::string(Color::BG_BLACK) + Color::WHITE;
                renderer.printW(bx + 4, by + 3 + i, getItemName(player_.bag[i].id), color);
            }
        }
        if (itemMsg_ && itemMsgTimer_ > 0) {
            renderer.printW(bx + 2, by + bh - 4, itemMsg_,
                std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
        }
        renderer.printW(bx + 2, by + bh - 2, L"[Z]: 사용  [BS]: 뒤로",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    } else if (menuState_ == InGameMenuState::ITEM_TARGET) {
        int bw = 44, bh = 14, bx = W/2 - 22, by = 5;
        renderer.printW(bx + bw/2 - 5, by + 1, L"누구에게 사용?",
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        for (int i = 0; i < player_.partySize; i++) {
            const Pokemon& p = player_.party[i];
            if (!p.species) continue;
            bool sel = (i == itemTargetCursor_);
            std::string color = sel
                ? std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW
                : std::string(Color::BG_BLACK) + Color::WHITE;
            wchar_t lineBuf[64];
            swprintf(lineBuf, 64, L"%ls  HP %d/%d", p.species->name, p.currentHP, p.maxHP);
            renderer.printW(bx + 4, by + 3 + i, lineBuf, color);
        }
        renderer.printW(bx + 2, by + bh - 2, L"[Z]: 사용  [BS]: 뒤로",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    } else if (menuState_ == InGameMenuState::ITEM_MESSAGE) {
        // 가방 박스를 배경으로 유지 + 화면 하단 전체 폭 대화상자 오버레이.
        // 첫 줄: 레벨업 메시지. 둘째 줄(있을 때): 새 기술 습득 메시지. A로 다음/종료.
        int bw = 44, bx = W/2 - 22, by = 5;
        renderer.printW(bx + bw/2 - 2, by + 1, L"가방",
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        for (int i = 0; i < player_.bagSize; i++) {
            renderer.printW(bx + 4, by + 3 + i, getItemName(player_.bag[i].id),
                std::string(Color::BG_BLACK) + Color::WHITE);
        }
        // 하단 대화상자
        int H = renderer.height;
        int dlgH = 5;
        int dlgY = H - dlgH;
        renderer.fillRect(0, dlgY, W, dlgH, ' ',
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        // 상단 경계선
        for (int x = 0; x < W; x++)
            renderer.setCell(x, dlgY, '-',
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        if (itemMsg_) {
            renderer.printW(2, dlgY + 1, itemMsg_,
                std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
        }
        if (itemMsg2_) {
            renderer.printW(2, dlgY + 2, itemMsg2_,
                std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
        }
        renderer.printW(2, dlgY + dlgH - 1, L"[ Z / Enter: 계속 ]",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    } else if (menuState_ == InGameMenuState::ITEM_LEARN_SELECT) {
        // 잊을 기술 선택 UI — 화면 하단에 4기술 + "배우지 않기" 옵션 나열.
        int bw = 44, bx = W/2 - 22, by = 5;
        renderer.printW(bx + bw/2 - 2, by + 1, L"가방",
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        for (int i = 0; i < player_.bagSize; i++) {
            renderer.printW(bx + 4, by + 3 + i, getItemName(player_.bag[i].id),
                std::string(Color::BG_BLACK) + Color::WHITE);
        }
        int H = renderer.height;
        int dlgH = 9;
        int dlgY = H - dlgH;
        renderer.fillRect(0, dlgY, W, dlgH, ' ',
            std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        for (int x = 0; x < W; x++)
            renderer.setCell(x, dlgY, '-',
                std::string(Color::BG_BLACK) + Color::BRIGHT_WHITE);
        // 안내 메시지
        if (pendingLearnTargetIdx_ >= 0 && pendingLearnTargetIdx_ < player_.partySize) {
            const Pokemon& tgt = player_.party[pendingLearnTargetIdx_];
            wchar_t buf[128];
            swprintf(buf, 128, L"잊을 기술을 골라줘. (새 기술: %ls)",
                getMoveData(pendingLearnNewMid_).name);
            renderer.printW(2, dlgY + 1, buf,
                std::string(Color::BG_BLACK) + Color::BRIGHT_CYAN);
            // 4 기술
            for (int i = 0; i < tgt.numMoves; i++) {
                bool sel = (pendingLearnCursor_ == i);
                std::string color = std::string(Color::BG_BLACK) +
                    (sel ? Color::BRIGHT_YELLOW : Color::WHITE);
                wchar_t line[80];
                const MoveData& mv = getMoveData(tgt.moves[i].moveId);
                swprintf(line, 80, L"%ls %ls  (PP %d/%d)",
                    sel ? L"▶" : L"  ", mv.name, tgt.moves[i].pp, mv.maxPP);
                renderer.printW(2, dlgY + 3 + i, line, color);
            }
            // "배우지 않기" 옵션
            bool skipSel = (pendingLearnCursor_ == tgt.numMoves);
            std::string skipColor = std::string(Color::BG_BLACK) +
                (skipSel ? Color::BRIGHT_YELLOW : Color::WHITE);
            wchar_t skipLine[40];
            swprintf(skipLine, 40, L"%ls 배우지 않기", skipSel ? L"▶" : L"  ");
            renderer.printW(2, dlgY + 3 + tgt.numMoves, skipLine, skipColor);
        }
        renderer.printW(2, dlgY + dlgH - 1, L"[↑↓] 선택  [Z] 결정  [BS] 포기",
            std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
    }
}
