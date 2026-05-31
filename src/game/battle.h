#pragma once
#include "player.h"
#include "../engine/renderer.h"
#include "../engine/input.h"

enum class BattleType { WILD, TRAINER, BOSS };

enum class BattlePhase {
    INTRO_TRANSITION,// 배틀 시작 효과 (검정/흰 깜빡임 → SHOW_MSG)
    CHOOSE_ACTION,   // 커맨드 선택 (싸운다/가방/포켓몬/도망)
    CHOOSE_MOVE,     // 기술 선택
    CHOOSE_ITEM,     // 아이템 선택
    CHOOSE_POKEMON,  // 파티에서 포켓몬 교체 선택
    EXECUTE_PLAYER,  // 플레이어 행동 실행
    EXECUTE_ENEMY,   // 적 행동 실행
    SHOW_MSG,        // 메시지 표시 후 다음 단계로
    LEVEL_UP_MSG,    // 레벨업 메시지
    LEARN_MOVE,      // 기술 4개 가득 찼을 때 교체 팝업
    EXP_OTHERS,      // 막타 외 포켓몬들의 경험치 획득 메시지 순차 표시
    FAINT_PLAYER,    // 내 포켓몬 기절
    FAINT_ENEMY,     // 적 포켓몬 기절
    VICTORY,         // 승리
    DEFEAT,          // 패배
    ESCAPED,         // 도망
    DONE,            // 배틀 종료 (다음 씬으로)
};

enum class BattleResult {
    NONE, WIN, LOSE, ESCAPE
};

struct BattleState {
    BattleType   type;
    Pokemon      enemy;          // 현재 상대 포켓몬
    int          enemyPartyIdx;  // 트레이너 파티에서 몇 번째
    Pokemon      enemyParty[3];
    int          enemyPartySize;
    const wchar_t* trainerName;
    const wchar_t* trainerPreText; // 배틀 전 대사
    int            trainerIntroId; // 풀바디 스프라이트 ID (0=OAK,1=RIVAL,2=RED,3=BROCK,4=BUG_CATCHER,5=COOLTRAINER_M)
    int            trainerPrize;   // 격파 시 상금 ($) — 트레이너/보스 승리 시 지급

    int          playerPartyIdx; // 현재 내 포켓몬 인덱스

    BattlePhase  phase;
    BattleResult result;
    int          cursor;     // 커맨드/기술 커서
    bool         awaitKey;   // 키 입력 대기

    wchar_t      msg[128];   // 현재 메시지 (한글)
    wchar_t      msg2[128];  // 두 번째 줄
    int          msgWait;    // 메시지 대기 프레임

    int          pendingMoveId; // 선택한 기술
    bool         playerFirst;   // 플레이어가 먼저 행동하는지

    bool         expGained;
    int          expAmount;
    bool         leveledUp;
    int          newLevel;

    // 경험치 전체 분배 — 포켓몬별 획득량/획득여부, 순차 메시지 인덱스
    int          expGainAmt[6];   // 각 파티 슬롯이 이번에 받은 경험치
    bool         expGotExp[6];    // 받았는지(살아있어 분배 대상)
    int          expOtherIdx;     // EXP_OTHERS 순차 출력 진행 인덱스

    bool         enemyWentFirst;   // static 제거 → 인스턴스 멤버
    bool         switchAfterFaint; // 기절 후 강제 교체 여부
    int          escapeAttempts;   // 이번 배틀 도망 시도 횟수 (원작 도망 확률용)

    // 기술 교체 팝업 (LEARN_MOVE) — 막타/비막타 포켓몬 공용
    int          learnQueue[8];    // 레벨업으로 배울 기술 ID 대기열 (4개 가득 찬 경우)
    int          learnQueuePoke[8];// 각 대기열 항목의 대상 포켓몬 파티 인덱스
    int          learnQueueCnt;    // 대기열 개수
    int          learnQueueIdx;    // 현재 처리 중인 대기열 인덱스
    int          learnForgetCursor;// 잊을 기술 선택 커서 (0~3 기술, 4=포기)
    int          learnSubPhase;    // 0=안내 메시지, 1=잊을 기술 선택, 2=결과 메시지

    int          itemMode;              // CHOOSE_ITEM 서브상태: 0=아이템선택, 1=상처약 파티 대상 선택
    int          dispEnemyHP;           // 화면 표시용 HP(실제값으로 매 프레임 슬라이드) — 적
    int          dispPlayerHP;          // 〃 — 내 포켓몬
    int          frame;
    int          transitionFrame;       // INTRO_TRANSITION 카운트다운
    bool         turnStarted;           // CHOOSE_MOVE에서 행동 결정됐는가 (false면 SHOW_MSG는 인사 메시지)
    bool         trainerIntroShown;     // 트레이너 풀바디 인트로 1회만 표시
};

class Battle {
public:
    Battle(Renderer& r, Player& pl);

    // 야생 배틀 시작
    void startWild(int speciesId, int level);
    // 트레이너 배틀 시작 (introId=풀바디 스프라이트 인덱스, 기본 0=OAK)
    void startTrainer(const wchar_t* name, const wchar_t* preText,
                      int* partyIds, int* partyLevels, int partySize,
                      int introId = 0, int prize = 0);
    // 보스(브록) 배틀
    void startBrock();

    void update(Key key);
    void render();
    void renderKorean();

    BattleResult result() const { return state_.result; }
    bool isDone() const { return state_.phase == BattlePhase::DONE; }

private:
    Renderer& ren_;
    Player&   pl_;
    BattleState state_;

    void setMsg(const wchar_t* m, const wchar_t* m2 = nullptr);
    void nextPhase(BattlePhase p, int waitFrames = 40);

    void executePlayerMove();
    void executeEnemyMove();
    void applyDamage(Pokemon& attacker, Pokemon& defender, int moveId, bool& superEff, bool& notEff, bool& noEff, int& damage);
    void applyStatEffect(Pokemon& target, int effect);
    void checkFaint();
    int  battleBagVisible(int* outSlots) const;  // 전투에서 보이는 가방 슬롯(이상한사탕 등 제외) — 개수 반환
    void grantExp();
    void advanceAfterFaint();           // 적 기절 후 다음 상대 또는 승리 처리
    void checkLevelUp(Pokemon& p);
    void silentLevelUp(Pokemon& p, int pokeIdx, bool allowQueue = true); // 막타 외 — 레벨업(4칸 가득: allowQueue면 교체 큐, 아니면 FIFO 자동)
    int  nextLearnableMove(Pokemon& p, int pokeIdx); // learnset 순서상 아직 안 배운 다음 기술 (없으면 0)
    void startLearnMoveQueue();         // 경험치 메시지 후 기술 교체 팝업 큐 처리 시작
    void setupLearnMoveItem();          // 현재 learnQueueIdx 항목에 맞춰 메시지/서브페이즈 셋업 (빈슬롯=즉시습득+결과, 4가득=잊을기술 선택)
    int  chooseEnemyMove();
    void drawHPBar(int x, int y, int cur, int maxHP, const std::string& color);
    void drawSprite(int x, int y, int speciesId, bool back);
    void syncHPDisplay();   // 표시용 HP를 현재 전투원 실제 HP로 즉시 동기화(전투원 교체 시)
};
