#pragma once
#include "../engine/renderer.h"
#include "../engine/input.h"
#include "../engine/audio.h"
#include "player.h"
#include "battle.h"
#include "overworld.h"
#include <string>

enum class Scene {
    INTRO_MOVIE,        // 오프닝 애니메이션 (게임프리크 → 니도리노 vs 팬텀 → 타이틀)
    TITLE,              // 타이틀 화면 (포켓몬 로고 + 타이틀 몬 + PRESS START)
    INTRO,              // 오박사 나레이션 (전/후반 공용)
    NAME_INPUT,         // 플레이어 이름 입력
    RIVAL_NAME_INPUT,   // 라이벌 이름 입력
    LAB_INTRO,          // 연구소 인트로 (오박사 설명 + 블루 등장)
    STARTER_SELECT,     // 스타터 선택
    RIVAL_INTERCEPT,    // 스타터 받고 출구 향할 때 블루가 가로막음
    RIVAL_BATTLE,       // 라이벌 배틀 (강제)
    RECEIVE_DEX,        // 포켓덱스 수령 대사
    OVERWORLD,          // 오버월드 탐험
    WILD_BATTLE,        // 야생 포켓몬 배틀
    TRAINER_BATTLE,     // 트레이너 배틀
    BOSS_BATTLE,        // 브록 배틀
    POKEMON_CENTER,     // 포켓몬센터 이벤트
    MART_EVENT,         // 마트 소포 이벤트
    MART_SHOP,          // 마트 구매 화면
    GAME_OVER,          // 전멸
    ENDING,             // 브록 클리어 엔딩
    WARP_MENU,          // 디버그: Ctrl+M 워프 메뉴
    INGAME_MENU,        // M 키 — 인게임 메뉴
};

enum class InGameMenuState {
    TOP_LEVEL,     // 최상위 (포켓몬/아이템/포켓덱스)
    PARTY_VIEW,    // 파티 목록
    PARTY_DETAIL,  // 포켓몬 상세
    ITEM_BAG,      // 가방 (아이템 리스트 + 사용)
    ITEM_TARGET,   // 상처약 사용 대상 파티 선택
    ITEM_MESSAGE,  // 아이템 사용 결과 대화상자 (이상한사탕 레벨업/기술습득) — A 입력으로 다음/종료
    ITEM_LEARN_SELECT, // 이상한사탕 4가득 — 잊을 기술 선택 화면
    POKEDEX,       // 포켓덱스 목록
    POKEDEX_DETAIL,// 포켓덱스 상세
};

class Game {
public:
    Game();
    void run();

    Renderer renderer;
    bool running = true;

private:
    Scene    scene_   = Scene::INTRO_MOVIE;
    int      frame_   = 0;
    int      volMsgTimer_ = 0;   // 볼륨 표시 잔여 프레임
    Player   player_  = {};
    Battle*  battle_  = nullptr;
    Overworld* ow_    = nullptr;

    void changeScene(Scene next);
    void update(Key key);
    void updateBGM();        // 씬/맵에 맞는 BGM 선택 (매 프레임)
    void render();
    void renderKorean();

    // ─── 인트로 ───
    static const int INTRO_COUNT = 12;
    static const wchar_t* INTRO_LINES[INTRO_COUNT];
    int  introStep_     = 0;

    void updateIntroMovie(Key key);
    void renderIntroMovie();
    void renderIntroMovieKorean();
    void updateTitle(Key key);
    void renderTitle();
    void renderTitleKorean();
    void updateIntro(Key key);
    void renderIntro();
    void renderIntroKorean();

    // ─── 이름 입력 ───
    char     nameBuf_[16] = {};
    int      nameLen_     = 0;

    void updateNameInput(Key key, char ch);
    void renderNameInput();
    void renderNameInputKorean();

    // ─── 라이벌 이름 입력 ───
    char     rivalNameBuf_[16] = {};
    int      rivalNameLen_     = 0;

    void updateRivalNameInput(Key key, char ch);
    void renderRivalNameInput();
    void renderRivalNameInputKorean();

    // ─── 스타터 선택 ───
    int  starterCursor_ = 0;

    void updateStarterSelect(Key key);
    void renderStarterSelect();
    void renderStarterSelectKorean();

    // ─── 라이벌 배틀 ───
    bool rivalBattleInit_ = false;

    // ─── 라이벌 인터셉트 ───
    static const int RIVAL_INTERCEPT_COUNT = 4;
    static const wchar_t* RIVAL_INTERCEPT_LINES[RIVAL_INTERCEPT_COUNT];
    int rivalInterceptStep_ = 0;
    void updateRivalIntercept(Key key);
    void renderRivalIntercept();
    void renderRivalInterceptKorean();

    // ─── 연구소 인트로 ───
    static const int LAB_INTRO_COUNT = 5;
    static const wchar_t* LAB_INTRO_LINES[LAB_INTRO_COUNT];
    int labIntroStep_ = 0;
    void updateLabIntro(Key key);
    void renderLabIntro();
    void renderLabIntroKorean();

    // ─── 포켓덱스 수령 ───
    int  dexStep_ = 0;
    static const wchar_t* DEX_LINES[5];

    void updateReceiveDex(Key key);
    void renderReceiveDex();
    void renderReceiveDexKorean();

    // ─── 포켓몬센터 ───
    int  centerStep_ = 0;

    void updateCenter(Key key);
    void renderCenter();
    void renderCenterKorean();

    // ─── 마트 이벤트 ───
    int  martStep_ = 0;

    void updateMart(Key key);
    void renderMart();
    void renderMartKorean();

    // ─── 마트 상점 ───
    int  shopMartId_     = 0;
    int  shopCursor_     = 0;       // 0..numItems(아이템) | numItems(=나간다)
    int  shopMode_       = 0;       // 0=메뉴, 1=수량 선택
    int  shopQuantity_   = 1;
    const wchar_t* shopMsg_ = nullptr;
    int  shopMsgTimer_   = 0;

    void updateMartShop(Key key);
    void renderMartShop();
    void renderMartShopKorean();

    // ─── 게임 오버 ───
    void renderGameOver();
    void updateGameOver(Key key);

    // ─── 디버그 워프 메뉴 ───
    int warpCursor_ = 0;
    void updateWarpMenu(Key key);
    void renderWarpMenuKorean();

    // ─── 인게임 메뉴 (M 키) ───
    Scene           prevScene_        = Scene::OVERWORLD;
    InGameMenuState menuState_        = InGameMenuState::TOP_LEVEL;
    int             menuCursor_       = 0;
    int             partyMenuCursor_  = 0;
    int             detailPartyIdx_   = 0;
    int             detailMoveCursor_ = 0;   // 상세 화면 기술/액션 커서
    int             detailSwapSel_    = -1;  // 기술 순서 교체용 첫 선택 슬롯(-1=없음)
    int             itemMenuCursor_   = 0;
    int             itemTargetCursor_ = 0;
    const wchar_t*  itemMsg_          = nullptr;
    const wchar_t*  itemMsg2_         = nullptr;  // ITEM_MESSAGE 두 번째 줄(예: 새 기술 습득)
    wchar_t         itemMsgBuf_[128]  = {0};  // 동적 아이템 메시지(예: 이상한사탕 레벨업)
    wchar_t         itemMsgBuf2_[128] = {0};  // 두 번째 줄(기술 습득 메시지 등)
    int             itemMsgTimer_     = 0;
    // 이상한사탕 — 4가득 슬롯 잊을 기술 선택 흐름
    int             pendingLearnTargetIdx_ = 0;  // 학습 대상 파티 인덱스
    int             pendingLearnNewMid_    = 0;  // 새로 배우려는 기술 id (0 = 없음 / 대기 없음)
    int             pendingLearnCursor_    = 0;  // 잊을 기술 선택 커서 (0..numMoves: numMoves=배우지 않기)
    int             dexCursor_        = 0;   // 포켓덱스 목록 커서 (SPECIES 인덱스)
    int             dexDetailIdx_     = 0;   // 포켓덱스 상세 대상 (SPECIES 인덱스)
    void updateInGameMenu(Key key);
    void renderInGameMenu();
    void renderInGameMenuKorean();

    // ─── 엔딩 ───
    int endingStep_ = 0;

    void updateEnding(Key key);
    void renderEnding();
    void renderEndingKorean();

    // ─── 공통 대화창 ───
    const wchar_t* dialogLines_[6] = {};
    int  dialogCount_  = 0;
    int  dialogStep_   = 0;
    Scene dialogNext_  = Scene::OVERWORLD;

    void startDialog(const wchar_t** lines, int count, Scene next);
};
