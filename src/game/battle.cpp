#include "battle.h"
#include "../data/type_chart.h"
#include "../data/sprites.h"
#include "../engine/audio.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <wchar.h>
#ifndef swprintf
#define swprintf _snwprintf
#endif

static int stageMultNum(int stage) {
    // 스테이지 -6~+6, 분자 반환 (분모=8)
    static const int tbl[13] = {2,2,3,3,4,5,6,7,8,9,10,11,12}; // stage+6 → tbl[stage+6]
    return tbl[stage + 6];
}
static int applyStage(int stat, int stage) {
    if (stage == 0) return stat;
    if (stage > 0) return stat * stageMultNum(stage) / 8;
    return stat * 8 / stageMultNum(-stage);
}

static bool specialType(Type t) {
    return t == Type::FIRE || t == Type::WATER || t == Type::GRASS ||
           t == Type::ELECTRIC || t == Type::PSYCHIC;
}

Battle::Battle(Renderer& r, Player& pl)
    : ren_(r), pl_(pl), state_{}
{}

void Battle::startWild(int speciesId, int level) {
    state_ = {};
    state_.type = BattleType::WILD;
    state_.enemy = makePokemon(speciesId, level);
    state_.enemyPartySize = 1;
    state_.enemyParty[0] = state_.enemy;
    state_.enemyPartyIdx = 0;
    state_.playerPartyIdx = firstAlive(pl_);
    state_.phase = BattlePhase::INTRO_TRANSITION;  // 화면 효과 → SHOW_MSG
    state_.transitionFrame = 18;
    state_.result = BattleResult::NONE;
    state_.cursor = 0;
    state_.awaitKey = false;  // transition 중엔 입력 차단
    state_.frame = 0;
    swprintf(state_.msg, 128, L"야생 %ls이(가) 나타났다!", state_.enemy.species->name);
    state_.msgWait = 0;
    syncHPDisplay();
}

void Battle::startTrainer(const wchar_t* name, const wchar_t* preText,
                          int* ids, int* levels, int sz, int introId, int prize) {
    state_ = {};
    state_.type = BattleType::TRAINER;
    state_.trainerName = name;
    state_.trainerPreText = preText;
    state_.trainerIntroId = introId;
    state_.trainerPrize = prize;
    state_.enemyPartySize = sz;
    for (int i = 0; i < sz && i < 3; i++)
        state_.enemyParty[i] = makePokemon(ids[i], levels[i]);
    state_.enemyPartyIdx = 0;
    state_.enemy = state_.enemyParty[0];
    state_.playerPartyIdx = firstAlive(pl_);
    state_.phase = BattlePhase::INTRO_TRANSITION;
    state_.transitionFrame = 18;
    state_.result = BattleResult::NONE;
    state_.cursor = 0;
    state_.awaitKey = false;
    state_.frame = 0;
    swprintf(state_.msg, 128, L"%ls이(가) 승부를 걸어왔다!", name);
    state_.msgWait = 0;
    syncHPDisplay();
}

void Battle::startBrock() {
    int ids[]    = {74, 95};
    int lvls[]   = {12, 14};
    startTrainer(L"관장 브록", L"바위 포켓몬은 최강이다!", ids, lvls, 2, 3, 1000);  // 3=BROCK intro, 상금 1000
    state_.type = BattleType::BOSS;
}

void Battle::syncHPDisplay() {
    state_.dispEnemyHP  = state_.enemy.currentHP;
    state_.dispPlayerHP = pl_.party[state_.playerPartyIdx].currentHP;
}

void Battle::setMsg(const wchar_t* m, const wchar_t* m2) {
    wcsncpy(state_.msg,  m  ? m  : L"", 127);
    wcsncpy(state_.msg2, m2 ? m2 : L"", 127);
    state_.awaitKey = true;
}

void Battle::nextPhase(BattlePhase p, int) {
    state_.phase = p;
    state_.awaitKey = false;
}

// ─── 데미지 계산 ─────────────────────────────────────────────
void Battle::applyDamage(Pokemon& atk, Pokemon& def, int moveId,
                         bool& superEff, bool& notEff, bool& noEff, int& damage) {
    const MoveData& mv = getMoveData(moveId);
    superEff = notEff = noEff = false;
    damage = 0;
    if (mv.power == 0) return;

    bool spec = specialType(mv.type);
    int atkStat = spec ? applyStage(atk.spc, atk.atkStage)
                       : applyStage(atk.atk, atk.atkStage);
    int defStat = spec ? applyStage(def.spc, def.defStage)
                       : applyStage(def.def, def.defStage);
    if (defStat < 1) defStat = 1;

    int base = ((2 * atk.level / 5 + 2) * mv.power * atkStat / defStat) / 50 + 2;

    // 타입 상성
    int eff = getEffInt(mv.type, def.species->type1, def.species->type2);
    if (eff == 0) { noEff = true; damage = 0; return; }
    notEff  = (eff < 10);
    superEff = (eff >= 20);
    base = base * eff / 10;

    // STAB
    if (mv.type == atk.species->type1 || mv.type == atk.species->type2)
        base = base * 3 / 2;

    // 랜덤 85~100%
    int r = 85 + rand() % 16;
    base = base * r / 100;
    damage = base > 0 ? base : 1;
}

void Battle::applyStatEffect(Pokemon& target, int effect) {
    switch (effect) {
        case 1: if (target.atkStage > -6) target.atkStage--; break;
        case 2: if (target.defStage > -6) target.defStage--; break;
        case 3: if (target.speStage > -6) target.speStage--; break;
        case 4: if (target.defStage <  6) target.defStage++; break;
        case 5: if (target.accStage > -6) target.accStage--; break;
    }
}

int Battle::chooseEnemyMove() {
    Pokemon& en = state_.enemy;
    // 사용 가능한 기술 중 랜덤 선택
    int valid[4]; int cnt = 0;
    for (int i = 0; i < en.numMoves; i++)
        if (en.moves[i].pp > 0) valid[cnt++] = i;
    if (cnt == 0) return 0;
    return en.moves[valid[rand() % cnt]].moveId;
}

void Battle::grantExp() {
    // 적 포켓몬 처치 — 포켓덱스 처치 횟수 기록
    {
        int di = speciesIndex(state_.enemy.species->id);
        if (di >= 0) pl_.dexDefeated[di]++;
    }
    int base = state_.enemy.species->baseExp;
    int full = base * state_.enemy.level / 7;
    if (full < 1) full = 1;
    int small = full / 2;          // 출전(막타) 외 포켓몬은 소량
    if (small < 1) small = 1;

    for (int i = 0; i < 6; i++) { state_.expGainAmt[i] = 0; state_.expGotExp[i] = false; }

    // 전체 분배: 살아있는 모든 파티 포켓몬에게 (막타=전투경험치 그대로, 나머지=소량)
    for (int i = 0; i < pl_.partySize; i++) {
        if (pokemonFainted(pl_.party[i])) continue;
        int g = (i == state_.playerPartyIdx) ? full : small;
        pl_.party[i].exp += g;
        state_.expGainAmt[i] = g;
        state_.expGotExp[i]  = true;
        // 막타 친 포켓몬(playerPartyIdx)은 FAINT_ENEMY 흐름에서 인터랙티브 처리.
        // 나머지는 여기서 레벨업(4칸 가득 기술은 교체 큐에 적재 → EXP_OTHERS 후 팝업).
        if (i != state_.playerPartyIdx)
            silentLevelUp(pl_.party[i], i);
    }
    state_.expAmount = full;        // 막타 메시지용
    state_.expGained = true;
}

// 전투에서 보이는 가방 슬롯만 추림 (이상한사탕 등 오버월드 전용 아이템 제외)
int Battle::battleBagVisible(int* outSlots) const {
    int c = 0;
    for (int i = 0; i < pl_.bagSize; i++) {
        if (pl_.bag[i].id == ITEM_RARE_CANDY) continue;  // 전투 가방엔 표시 안 함
        outSlots[c++] = i;
    }
    return c;
}

// 적 기절 후 — 다음 상대 포켓몬 등장 또는 승리 처리
void Battle::advanceAfterFaint() {
    state_.enemyPartyIdx++;
    if (state_.enemyPartyIdx < state_.enemyPartySize) {
        state_.enemy = state_.enemyParty[state_.enemyPartyIdx];
        syncHPDisplay();   // 새 상대 — 체력바 슬라이드 없이 가득 찬 상태로 시작
        swprintf(state_.msg, 128, L"%ls! %ls 나와라!",
            state_.trainerName ? state_.trainerName : L"",
            state_.enemy.species->name);
        state_.msg2[0] = 0;
        state_.awaitKey = true;
        state_.phase = BattlePhase::SHOW_MSG;
        state_.playerFirst = true;  // 다음 SHOW_MSG에서 CHOOSE_ACTION으로
    } else {
        state_.result = BattleResult::WIN;
        state_.phase = BattlePhase::VICTORY;
        if (state_.type == BattleType::BOSS)
            swprintf(state_.msg, 128, L"브록을 물리쳤다! 바위배지를 획득!");
        else if (state_.type == BattleType::TRAINER)
            swprintf(state_.msg, 128, L"%ls을(를) 물리쳤다!", state_.trainerName);
        else
            swprintf(state_.msg, 128, L"승리했다!");
        // 트레이너/보스 격파 시 상금 지급 + 메시지
        if ((state_.type == BattleType::TRAINER || state_.type == BattleType::BOSS)
            && state_.trainerPrize > 0) {
            pl_.money += state_.trainerPrize;
            swprintf(state_.msg2, 128, L"상금 %d원을 손에 넣었다!", state_.trainerPrize);
        } else {
            state_.msg2[0] = 0;
        }
        state_.awaitKey = true;
    }
}

// 경험치 메시지 종료 후 — 기술 교체 팝업 대기열의 첫 항목으로 진입
void Battle::startLearnMoveQueue() {
    state_.learnQueueIdx     = 0;
    state_.learnForgetCursor = 0;
    state_.phase = BattlePhase::LEARN_MOVE;
    setupLearnMoveItem();
}

// 현재 learnQueueIdx 항목에 맞춰 메시지/서브페이즈 셋업.
// 빈 슬롯이면 즉시 슬롯에 추가하고 결과 메시지(subPhase 2)로 진입 — 사용자에게 "X을(를) 배웠다!" 안내.
// 4개 가득이면 안내 메시지(subPhase 0) → 잊을 기술 선택(subPhase 1) → 결과 메시지(subPhase 2).
void Battle::setupLearnMoveItem() {
    int pokeIdx = state_.learnQueuePoke[state_.learnQueueIdx];
    int mvId    = state_.learnQueue[state_.learnQueueIdx];
    Pokemon& pp = pl_.party[pokeIdx];
    if (pp.numMoves < 4) {
        // 빈 슬롯 — 즉시 습득 후 결과 메시지로 점프
        pp.moves[pp.numMoves].moveId = mvId;
        pp.moves[pp.numMoves].pp     = getMoveData(mvId).maxPP;
        pp.numMoves++;
        swprintf(state_.msg, 128, L"%ls은(는) %ls을(를) 배웠다!",
            pp.species->name, getMoveData(mvId).name);
        state_.msg2[0] = 0;
        state_.learnSubPhase = 2;
    } else {
        // 4개 가득 — 잊을 기술 선택 흐름
        swprintf(state_.msg, 128, L"%ls은(는) 새로운 기술 %ls을(를) 배우고 싶어한다!",
            pp.species->name, getMoveData(mvId).name);
        swprintf(state_.msg2, 128, L"하지만 기술은 4개까지만 배울 수 있다!");
        state_.learnSubPhase = 0;
    }
    state_.awaitKey = true;
}

// learnset 순서상 아직 안 배운(그리고 대기열에도 없는) 다음 기술 id
// 단, 자신의 약점 속성(자기 타입에게 효과가 굉장한 타입) 기술은 배우지 않는다.
int Battle::nextLearnableMove(Pokemon& p, int pokeIdx) {
    if (!p.species) return 0;
    for (int i = 0; i < 8; i++) {
        const LearnMove& lm = p.species->learnset[i];
        if (lm.level == 0 && lm.moveId == 0) break;
        int mid = lm.moveId;
        if (mid == 0) continue;
        bool has = pokemonHasMove(p, mid);
        // 같은 포켓몬의 교체 대기열에 이미 있으면 제외
        for (int j = 0; j < state_.learnQueueCnt; j++)
            if (state_.learnQueuePoke[j] == pokeIdx && state_.learnQueue[j] == mid) { has = true; break; }
        if (has) continue;
        // 약점 속성 기술 제외: 기술 타입이 내 타입에게 효과가 굉장하면(>=2x) 건너뜀
        if (moveIsStrongAgainstSelf(p, mid)) continue;
        return mid;
    }
    for (int i = 1; i < NUM_MOVES_DATA; i++) {
        int mid = MOVES[i].id;
        if (mid <= 0) continue;
        bool has = pokemonHasMove(p, mid);
        for (int j = 0; j < state_.learnQueueCnt; j++)
            if (state_.learnQueuePoke[j] == pokeIdx && state_.learnQueue[j] == mid) { has = true; break; }
        if (has) continue;
        if (getEffInt(MOVES[i].type, p.species->type1, p.species->type2) >= 20) continue;
        return mid;
    }
    return 0;
}

// 막타 외 포켓몬 — 레벨업 + 3의 배수 기술 획득
// 빈/가득 무관 항상 교체 팝업 대기열에 추가 → setupLearnMoveItem이 빈슬롯=즉시습득+메시지, 4가득=잊을기술 선택으로 분기.
// allowQueue=false (포획 등 팝업 흐름이 없는 경우) → 빈 슬롯 자동 추가, 가득 시 가장 오래된 기술 밀어내기.
void Battle::silentLevelUp(Pokemon& p, int pokeIdx, bool allowQueue) {
    while (p.exp >= expForLevel(p.level + 1)) {
        p.level++;
        recalcStats(p);
        if (p.currentHP > p.maxHP) p.currentHP = p.maxHP;
        if (p.level % 3 == 0) {
            int mid = nextLearnableMove(p, pokeIdx);
            if (mid != 0) {
                if (allowQueue) {
                    if (state_.learnQueueCnt < 8) {
                        state_.learnQueuePoke[state_.learnQueueCnt] = pokeIdx;
                        state_.learnQueue[state_.learnQueueCnt]     = mid;
                        state_.learnQueueCnt++;
                    }
                } else {
                    // 팝업 흐름 없음(포획 등) → 자동 처리
                    if (p.numMoves < 4) {
                        p.moves[p.numMoves].moveId = mid;
                        p.moves[p.numMoves].pp = getMoveData(mid).maxPP;
                        p.numMoves++;
                    } else {
                        for (int k = 0; k < 3; k++) p.moves[k] = p.moves[k + 1];
                        p.moves[3].moveId = mid;
                        p.moves[3].pp = getMoveData(mid).maxPP;
                    }
                }
            }
        }
    }
}

void Battle::checkLevelUp(Pokemon& p) {
    state_.leveledUp = false;
    while (p.exp >= expForLevel(p.level + 1)) {
        p.level++;
        recalcStats(p);
        if (p.currentHP > p.maxHP) p.currentHP = p.maxHP;
        // 3의 배수 레벨마다 새 기술 1개 획득 후보를 큐에 추가 (learnset 순서대로).
        // 빈 슬롯이든 4가득이든 큐를 통해 일관된 메시지 흐름으로 처리한다.
        if (p.level % 3 == 0) {
            int mid = nextLearnableMove(p, state_.playerPartyIdx);
            if (mid != 0 && state_.learnQueueCnt < 8) {
                state_.learnQueuePoke[state_.learnQueueCnt] = state_.playerPartyIdx;
                state_.learnQueue[state_.learnQueueCnt]     = mid;
                state_.learnQueueCnt++;
            }
        }
        state_.leveledUp = true;
        state_.newLevel = p.level;
    }
}

// ─── 행동 실행 ────────────────────────────────────────────────
void Battle::executePlayerMove() {
    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;
    int mid = state_.pendingMoveId;
    const MoveData& mv = getMoveData(mid);

    // PP 소모
    for (int i = 0; i < myPoke.numMoves; i++)
        if (myPoke.moves[i].moveId == mid) { myPoke.moves[i].pp--; break; }

    // 명중 판정
    int acc = mv.accuracy;
    // acc stage 적용 (간략화)
    bool hit = ((rand() % 100) < acc);
    if (!hit) {
        swprintf(state_.msg, 128, L"공격이 빗나갔다!");
        state_.msg2[0] = 0;
        state_.phase = BattlePhase::SHOW_MSG;
        state_.awaitKey = true;
        return;
    }

    if (mv.power > 0) {
        bool se, ne, noe; int dmg;
        applyDamage(myPoke, en, mid, se, ne, noe, dmg);
        if (noe) {
            swprintf(state_.msg, 128, L"%ls의 %ls!", myPoke.species->name, mv.name);
            swprintf(state_.msg2, 128, L"효과가 없다!");
        } else {
            en.currentHP -= dmg;
            if (en.currentHP < 0) en.currentHP = 0;
            Audio::playSE("se_bump");   // 공격 명중 효과음
            swprintf(state_.msg, 128, L"%ls의 %ls!", myPoke.species->name, mv.name);
            if (se) swprintf(state_.msg2, 128, L"효과가 굉장했다!");
            else if (ne) swprintf(state_.msg2, 128, L"효과가 별로인 것 같다...");
            else state_.msg2[0] = 0;
        }
    } else {
        // 변화기술
        applyStatEffect(en, mv.effect);
        swprintf(state_.msg, 128, L"%ls의 %ls!", myPoke.species->name, mv.name);
        state_.msg2[0] = 0;
    }
    state_.phase = BattlePhase::SHOW_MSG;
    state_.awaitKey = true;
    // 다음은 checkFaint → executeEnemy 순서로
}

void Battle::executeEnemyMove() {
    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;

    int mid = chooseEnemyMove();
    const MoveData& mv = getMoveData(mid);

    // PP 소모 (적)
    for (int i = 0; i < en.numMoves; i++)
        if (en.moves[i].moveId == mid) { en.moves[i].pp--; break; }

    bool hit = ((rand() % 100) < mv.accuracy);
    if (!hit) {
        swprintf(state_.msg, 128, L"상대의 %ls이(가) 빗나갔다!", mv.name);
        state_.msg2[0] = 0;
        state_.phase = BattlePhase::SHOW_MSG;
        state_.awaitKey = true;
        return;
    }

    if (mv.power > 0) {
        bool se, ne, noe; int dmg;
        applyDamage(en, myPoke, mid, se, ne, noe, dmg);
        if (noe) {
            swprintf(state_.msg, 128, L"상대 %ls의 %ls!", en.species->name, mv.name);
            swprintf(state_.msg2, 128, L"효과가 없다!");
        } else {
            myPoke.currentHP -= dmg;
            if (myPoke.currentHP < 0) myPoke.currentHP = 0;
            Audio::playSE("se_bump");   // 피격 효과음
            swprintf(state_.msg, 128, L"상대 %ls의 %ls!", en.species->name, mv.name);
            if (se) swprintf(state_.msg2, 128, L"효과가 굉장했다!");
            else if (ne) swprintf(state_.msg2, 128, L"효과가 별로인 것 같다...");
            else state_.msg2[0] = 0;
        }
    } else {
        applyStatEffect(myPoke, mv.effect);
        swprintf(state_.msg, 128, L"상대 %ls의 %ls!", en.species->name, mv.name);
        state_.msg2[0] = 0;
    }
    state_.phase = BattlePhase::SHOW_MSG;
    state_.awaitKey = true;
}

// ─── 업데이트 ────────────────────────────────────────────────
void Battle::update(Key key) {
    state_.frame++;
    BattlePhase prevPhase = state_.phase;  // phase 변경 감지 (한글 잔상 정리용)
    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;

    // 체력바 슬라이드: 표시용 HP를 실제 HP 쪽으로 매 프레임 조금씩 이동 (약 24프레임에 풀바)
    auto easeHP = [](int& disp, int target, int maxHP) {
        if (disp == target) return;
        int step = maxHP / 24; if (step < 1) step = 1;
        if (disp > target) { disp -= step; if (disp < target) disp = target; }
        else               { disp += step; if (disp > target) disp = target; }
    };
    easeHP(state_.dispPlayerHP, myPoke.currentHP, myPoke.maxHP);
    easeHP(state_.dispEnemyHP,  en.currentHP,     en.maxHP);

    switch (state_.phase) {

    case BattlePhase::INTRO_TRANSITION: {
        // 검정/흰 깜빡임 카운트다운. 0이 되면 SHOW_MSG로.
        state_.transitionFrame--;
        if (state_.transitionFrame <= 0) {
            state_.phase = BattlePhase::SHOW_MSG;
            state_.awaitKey = true;
        }
        break;
    }

    case BattlePhase::CHOOSE_ACTION: {
        // 2×2 그리드: 0=싸운다 1=가방 2=포켓몬 3=도망
        static const int ACTION_COLS = 2;
        static const int ACTION_ROWS = 2;
        if (key == Key::RIGHT) state_.cursor = (state_.cursor % ACTION_COLS == ACTION_COLS-1)
                                               ? state_.cursor - (ACTION_COLS-1)
                                               : state_.cursor + 1;
        if (key == Key::LEFT)  state_.cursor = (state_.cursor % ACTION_COLS == 0)
                                               ? state_.cursor + (ACTION_COLS-1)
                                               : state_.cursor - 1;
        if (key == Key::DOWN)  state_.cursor = (state_.cursor + ACTION_COLS) % (ACTION_COLS * ACTION_ROWS);
        if (key == Key::UP)    state_.cursor = (state_.cursor - ACTION_COLS + ACTION_COLS * ACTION_ROWS)
                                               % (ACTION_COLS * ACTION_ROWS);
        if (key == Key::A) {
            switch (state_.cursor) {
            case 0: // 싸운다
                state_.phase = BattlePhase::CHOOSE_MOVE;
                state_.cursor = 0;
                break;
            case 1: // 가방
                state_.phase = BattlePhase::CHOOSE_ITEM;
                state_.cursor = 0;
                state_.itemMode = 0;  // 0=아이템 선택, 1=상처약 대상 파티 선택
                break;
            case 2: // 포켓몬 교체
                state_.phase = BattlePhase::CHOOSE_POKEMON;
                state_.cursor = 0;
                break;
            case 3: // 도망
                if (state_.type == BattleType::WILD) {
                    // ── 원작(Gen1) 도망 확률 공식 ──
                    // F = (내속도*32) / ((적속도/4) mod 256) + 30*(시도횟수)
                    // 내속도>적속도 또는 (적속도/4)mod256==0 또는 F>=256 이면 무조건 성공
                    int mySpe = applyStage(myPoke.spe, myPoke.speStage);
                    int enSpe = applyStage(en.spe, en.speStage);
                    state_.escapeAttempts++;
                    bool escaped;
                    if (mySpe > enSpe) {
                        escaped = true;
                    } else {
                        int b = (enSpe / 4) % 256;
                        if (b <= 0) {
                            escaped = true;
                        } else {
                            int f = (mySpe * 32) / b + 30 * (state_.escapeAttempts - 1);
                            escaped = (f >= 256) || ((rand() % 256) < f);
                        }
                    }
                    if (escaped) {
                        state_.result = BattleResult::ESCAPE;
                        swprintf(state_.msg, 128, L"무사히 도망쳤다!");
                        state_.msg2[0] = 0;
                        state_.phase = BattlePhase::SHOW_MSG;
                        state_.awaitKey = true;
                    } else {
                        swprintf(state_.msg, 128, L"도망칠 수 없었다!");
                        state_.msg2[0] = 0;
                        state_.phase = BattlePhase::SHOW_MSG;
                        state_.awaitKey = true;
                        // 도망 실패 = 턴 소모 → SHOW_MSG 후 적 공격
                        state_.turnStarted    = true;
                        state_.playerFirst    = true;
                        state_.enemyWentFirst = false;
                    }
                } else {
                    swprintf(state_.msg, 128, L"트레이너 배틀에서는 도망칠 수 없다!");
                    state_.msg2[0] = 0;
                    state_.phase = BattlePhase::SHOW_MSG;
                    state_.awaitKey = true;
                }
                break;
            }
        }
        break;
    }

    case BattlePhase::CHOOSE_ITEM: {
        // 가방 메뉴 — itemMode 0=아이템 선택, 1=상처약 대상 파티 선택
        if (state_.itemMode == 0) {
            int slots[MAX_BAG_SLOTS];
            int n = battleBagVisible(slots);
            if (n == 0) {
                if (key == Key::A || key == Key::B) {
                    state_.phase = BattlePhase::CHOOSE_ACTION;
                    state_.cursor = 0;
                }
                break;
            }
            if (state_.cursor >= n) state_.cursor = n - 1;
            if (key == Key::UP)   state_.cursor = (state_.cursor - 1 + n) % n;
            if (key == Key::DOWN) state_.cursor = (state_.cursor + 1) % n;
            if (key == Key::B) {
                state_.phase = BattlePhase::CHOOSE_ACTION;
                state_.cursor = 0;
                break;
            }
            if (key == Key::A) {
                ItemId id = pl_.bag[slots[state_.cursor]].id;
                if (id == ITEM_POKE_BALL) {
                    if (state_.type != BattleType::WILD) {
                        swprintf(state_.msg, 128, L"트레이너 배틀에서는 몬스터볼을 쓸 수 없다!");
                        state_.msg2[0] = 0;
                        state_.phase = BattlePhase::SHOW_MSG;
                        state_.awaitKey = true;
                        break;
                    }
                    removeItem(pl_, ITEM_POKE_BALL, 1);
                    // 풀HP 30%, 반HP 70%, 25%HP 80%, 빈사 90%
                    int pct = (en.maxHP - en.currentHP) * 100 / en.maxHP;
                    int catchRate = (pct <= 50)
                        ? 30 + pct * 40 / 50
                        : 70 + (pct - 50) * 20 / 50;
                    if (rand() % 100 < catchRate && pl_.partySize < 6) {
                        pl_.party[pl_.partySize++] = en;
                        // 포켓덱스 포획 횟수 기록
                        {
                            int di = speciesIndex(en.species->id);
                            if (di >= 0) pl_.dexCaught[di]++;
                        }
                        // HP는 잡힌 상태 그대로 유지 (포켓몬센터에서 회복)
                        // 포획 시에도 경험치 분배 — 잡은 본인 제외, 기존 파티 전체에 지급
                        {
                            int base = en.species->baseExp;
                            int g = base * en.level / 7;
                            if (g < 1) g = 1;
                            for (int i = 0; i < pl_.partySize - 1; i++) {
                                if (pokemonFainted(pl_.party[i])) continue;
                                pl_.party[i].exp += g;
                                silentLevelUp(pl_.party[i], i, false);  // 포획: 팝업 흐름 없음 → 자동
                            }
                        }
                        swprintf(state_.msg, 128, L"%ls을(를) 잡았다!", en.species->name);
                        state_.msg2[0] = 0;
                        state_.phase = BattlePhase::SHOW_MSG;
                        state_.awaitKey = true;
                        state_.result = BattleResult::WIN;
                    } else {
                        swprintf(state_.msg, 128, L"아, 아쉽다! 조금만 더!");
                        state_.msg2[0] = 0;
                        state_.phase = BattlePhase::SHOW_MSG;
                        state_.awaitKey = true;
                        // 포획 실패 = 턴 소모 → SHOW_MSG 후 적 공격
                        state_.turnStarted    = true;
                        state_.playerFirst    = true;
                        state_.enemyWentFirst = false;
                    }
                } else if (id == ITEM_POTION) {
                    state_.itemMode = 1;
                    state_.cursor = state_.playerPartyIdx;
                }
            }
        } else {
            // itemMode == 1: 상처약 대상 파티 선택
            int n = pl_.partySize;
            if (key == Key::UP)   state_.cursor = (state_.cursor - 1 + n) % n;
            if (key == Key::DOWN) state_.cursor = (state_.cursor + 1) % n;
            if (key == Key::B) {
                state_.itemMode = 0;
                state_.cursor = 0;
                break;
            }
            if (key == Key::A) {
                Pokemon& tgt = pl_.party[state_.cursor];
                if (!tgt.species) {
                    // 빈 슬롯 — 무시
                    break;
                }
                if (tgt.currentHP <= 0) {
                    swprintf(state_.msg, 128, L"쓰러진 포켓몬에겐 효과가 없다!");
                    state_.msg2[0] = 0;
                    state_.phase = BattlePhase::SHOW_MSG;
                    state_.awaitKey = true;
                    state_.itemMode = 0;
                    state_.cursor = 0;
                    break;
                }
                if (tgt.currentHP >= tgt.maxHP) {
                    swprintf(state_.msg, 128, L"HP가 가득 차 있다!");
                    state_.msg2[0] = 0;
                    state_.phase = BattlePhase::SHOW_MSG;
                    state_.awaitKey = true;
                    state_.itemMode = 0;
                    state_.cursor = 0;
                    break;
                }
                int heal = POTION_HEAL_AMOUNT;
                if (tgt.currentHP + heal > tgt.maxHP) heal = tgt.maxHP - tgt.currentHP;
                tgt.currentHP += heal;
                removeItem(pl_, ITEM_POTION, 1);
                swprintf(state_.msg, 128, L"%ls의 HP가 %d 회복됐다!", tgt.species->name, heal);
                state_.msg2[0] = 0;
                state_.phase = BattlePhase::SHOW_MSG;
                state_.awaitKey = true;
                state_.itemMode = 0;
                state_.cursor = 0;
                // 상처약 사용 = 턴 소모 → SHOW_MSG 후 적 공격
                state_.turnStarted    = true;
                state_.playerFirst    = true;
                state_.enemyWentFirst = false;
            }
        }
        break;
    }

    case BattlePhase::CHOOSE_MOVE: {
        int nMoves = myPoke.numMoves;
        if (key == Key::RIGHT) state_.cursor = (state_.cursor + 1) % nMoves;
        if (key == Key::LEFT)  state_.cursor = ((state_.cursor - 1) + nMoves) % nMoves;
        if (key == Key::DOWN)  state_.cursor = (state_.cursor + 2 < nMoves) ? state_.cursor + 2 : state_.cursor;
        if (key == Key::UP)    state_.cursor = (state_.cursor - 2 >= 0) ? state_.cursor - 2 : state_.cursor;
        if (key == Key::A) {
            if (myPoke.moves[state_.cursor].pp > 0) {
                state_.pendingMoveId = myPoke.moves[state_.cursor].moveId;
                // 속도 비교로 선공 결정
                int mySpe = applyStage(myPoke.spe, myPoke.speStage);
                int enSpe = applyStage(en.spe, en.speStage);
                state_.playerFirst = (mySpe >= enSpe);
                state_.enemyWentFirst = false;  // 새 턴 시작 — flag reset
                state_.turnStarted = true;       // 행동 결정 — SHOW_MSG가 인사 X
                state_.phase = BattlePhase::EXECUTE_PLAYER;
                if (!state_.playerFirst)
                    state_.phase = BattlePhase::EXECUTE_ENEMY;
            }
        }
        if (key == Key::B) {
            state_.phase = BattlePhase::CHOOSE_ACTION;
            state_.cursor = 0;
        }
        break;
    }

    case BattlePhase::CHOOSE_POKEMON: {
        // 파티 선택 (현재 포켓몬 제외, 기절 제외)
        if (key == Key::UP)   state_.cursor = (state_.cursor - 1 + pl_.partySize) % pl_.partySize;
        if (key == Key::DOWN) state_.cursor = (state_.cursor + 1) % pl_.partySize;
        if (key == Key::A) {
            int sel = state_.cursor;
            if (sel == state_.playerPartyIdx) {
                swprintf(state_.msg, 128, L"이미 싸우고 있는 포켓몬이야!");
                state_.msg2[0] = 0;
                state_.phase = BattlePhase::SHOW_MSG;
                state_.awaitKey = true;
            } else if (pokemonFainted(pl_.party[sel])) {
                swprintf(state_.msg, 128, L"%ls은(는) 쓰러져서 싸울 수 없어!", pl_.party[sel].species->name);
                state_.msg2[0] = 0;
                state_.phase = BattlePhase::SHOW_MSG;
                state_.awaitKey = true;
            } else {
                state_.playerPartyIdx = sel;
                syncHPDisplay();   // 교체된 포켓몬 — 체력바 즉시 동기화
                swprintf(state_.msg, 128, L"%ls! 나가줘!", pl_.party[sel].species->name);
                state_.msg2[0] = 0;
                // 교체 후 적이 공격 (교체는 턴 소모)
                state_.phase = BattlePhase::SHOW_MSG;
                state_.awaitKey = true;
                state_.playerFirst = false; // 교체 후 적 선공
            }
        }
        if (key == Key::B) {
            // 기절 후 강제 교체가 아닌 경우에만 취소 가능
            if (!state_.switchAfterFaint) {
                state_.phase = BattlePhase::CHOOSE_ACTION;
                state_.cursor = 0;
            }
        }
        break;
    }

    case BattlePhase::EXECUTE_PLAYER:
        if (pokemonFainted(en)) {
            state_.phase = BattlePhase::FAINT_ENEMY;
            swprintf(state_.msg, 128, L"상대 %ls이(가) 쓰러졌다!", en.species->name);
            state_.msg2[0] = 0;
            state_.awaitKey = true;
            grantExp();
        } else {
            executePlayerMove();
            // 다음 단계는 SHOW_MSG → 적 행동
        }
        break;

    case BattlePhase::EXECUTE_ENEMY:
        if (pokemonFainted(myPoke)) {
            state_.phase = BattlePhase::FAINT_PLAYER;
            swprintf(state_.msg, 128, L"%ls이(가) 쓰러졌다!", myPoke.species->name);
            state_.msg2[0] = 0;
            state_.awaitKey = true;
        } else {
            executeEnemyMove();
        }
        break;

    case BattlePhase::SHOW_MSG:
        if (!state_.awaitKey) break;
        if (key == Key::A || key == Key::B) {
            // 잡기 성공 / 도망 성공 시 DONE
            if ((state_.result == BattleResult::WIN ||
                 state_.result == BattleResult::ESCAPE) &&
                state_.type == BattleType::WILD) {
                state_.phase = BattlePhase::DONE;
                break;
            }
            // 적 기절 체크
            if (pokemonFainted(en)) {
                state_.phase = BattlePhase::FAINT_ENEMY;
                swprintf(state_.msg, 128, L"상대 %ls이(가) 쓰러졌다!", en.species->name);
                state_.msg2[0] = 0;
                state_.awaitKey = true;
                grantExp();
                break;
            }
            // 내 포켓몬 기절 체크
            if (pokemonFainted(myPoke)) {
                state_.phase = BattlePhase::FAINT_PLAYER;
                swprintf(state_.msg, 128, L"%ls이(가) 쓰러졌다!", myPoke.species->name);
                state_.msg2[0] = 0;
                state_.awaitKey = true;
                break;
            }
            // 인사 메시지 ("승부를 걸어왔다", "야생 X 나타났다") — turnStarted false면 CHOOSE_ACTION으로
            if (!state_.turnStarted) {
                state_.trainerIntroShown = true;  // 트레이너 풀바디 인트로 1회 본 거 표시
                state_.phase = BattlePhase::CHOOSE_ACTION;
                state_.cursor = 0;
                break;
            }
            // 적 선공 시: EXECUTE_ENEMY → SHOW_MSG → EXECUTE_PLAYER → SHOW_MSG → CHOOSE_ACTION
            // 플레이어 선공 시: EXECUTE_PLAYER → SHOW_MSG → EXECUTE_ENEMY → SHOW_MSG → CHOOSE_ACTION
            if (!state_.playerFirst) {
                if (!state_.enemyWentFirst) {
                    // 적이 처음 행동 마침 → 이제 플레이어 차례 (이전: 적 또 행동 = 2번 버그)
                    state_.phase = BattlePhase::EXECUTE_PLAYER;
                    state_.enemyWentFirst = true;
                } else {
                    // 적+플레이어 다 끝 → 다음 턴
                    state_.enemyWentFirst = false;
                    state_.turnStarted = false;  // 턴 끝 — 다음 SHOW_MSG는 인사 또는 turn 시작 전
                    state_.phase = BattlePhase::CHOOSE_ACTION;
                    state_.cursor = 0;
                }
            } else {
                // 플레이어 선공 → 적 행동 한 번 → 다음 SHOW_MSG에서 CHOOSE_ACTION
                state_.phase = BattlePhase::EXECUTE_ENEMY;
                state_.playerFirst = false;
                state_.enemyWentFirst = true;  // 적이 이번 턴 행동했음 → 다음 SHOW_MSG에서 else 분기
            }
        }
        break;

    case BattlePhase::FAINT_ENEMY:
        if (key == Key::A || key == Key::B) {
            // 막타(출전) 포켓몬 경험치/레벨업 처리
            if (state_.expGained) {
                state_.expGained = false;
                Pokemon& pp = pl_.party[state_.playerPartyIdx];
                checkLevelUp(pp);
                if (state_.leveledUp) {
                    swprintf(state_.msg, 128, L"%ls이(가) 레벨 %d이(가) 되었다!",
                        pp.species->name, state_.newLevel);
                    state_.msg2[0] = 0;
                    state_.phase = BattlePhase::LEVEL_UP_MSG;
                    state_.awaitKey = true;
                    break;
                }
                // 레벨업 없음 → 막타 경험치 메시지 → 나머지 포켓몬 순차(EXP_OTHERS)
                swprintf(state_.msg, 128, L"%ls은(는) 경험치 %d를 획득했다!",
                    pp.species->name, state_.expAmount);
                state_.msg2[0] = 0;
                state_.phase = BattlePhase::EXP_OTHERS;
                state_.expOtherIdx = 0;
                state_.awaitKey = true;
                break;
            }
            // 경험치 분배 끝 → 다음 상대 또는 승리
            advanceAfterFaint();
        }
        break;

    case BattlePhase::EXP_OTHERS:
        if (key == Key::A || key == Key::B) {
            // 막타 외 포켓몬들의 경험치 획득 메시지를 하나씩 출력
            bool shown = false;
            while (state_.expOtherIdx < pl_.partySize) {
                int i = state_.expOtherIdx++;
                if (i == state_.playerPartyIdx) continue;
                if (!state_.expGotExp[i]) continue;
                swprintf(state_.msg, 128, L"%ls은(는) 경험치 %d를 획득했다!",
                    pl_.party[i].species->name, state_.expGainAmt[i]);
                state_.msg2[0] = 0;
                state_.awaitKey = true;
                shown = true;
                break;
            }
            if (!shown) {
                // 전원 경험치 출력 완료 → 기술 교체 팝업이 있으면 처리, 없으면 다음 상대/승리
                if (state_.learnQueueCnt > 0)
                    startLearnMoveQueue();
                else
                    advanceAfterFaint();
            }
        }
        break;

    case BattlePhase::FAINT_PLAYER:
        if (key == Key::A || key == Key::B) {
            // 살아있는 포켓몬이 있으면 강제 교체 화면으로
            bool hasAlive = false;
            for (int i = 0; i < pl_.partySize; i++)
                if (i != state_.playerPartyIdx && !pokemonFainted(pl_.party[i]))
                    { hasAlive = true; break; }

            if (hasAlive) {
                state_.switchAfterFaint = true;
                state_.phase = BattlePhase::CHOOSE_POKEMON;
                state_.cursor = 0;
            } else {
                state_.result = BattleResult::LOSE;
                state_.phase = BattlePhase::DEFEAT;
                swprintf(state_.msg, 128, L"모든 포켓몬이 쓰러졌다...");
                swprintf(state_.msg2, 128, L"포켓몬 센터로 달려갔다.");
                state_.awaitKey = true;
            }
        }
        break;

    case BattlePhase::LEVEL_UP_MSG:
        if (key == Key::A || key == Key::B) {
            Pokemon& pp = pl_.party[state_.playerPartyIdx];
            // 막타 경험치 메시지 → 나머지 포켓몬 순차(EXP_OTHERS).
            // 기술 교체 팝업(LEARN_MOVE)은 경험치 메시지가 모두 끝난 뒤 일괄 처리.
            swprintf(state_.msg, 128, L"%ls은(는) 경험치 %d를 획득했다!",
                pp.species->name, state_.expAmount);
            state_.msg2[0] = 0;
            state_.phase = BattlePhase::EXP_OTHERS;
            state_.expOtherIdx = 0;
            state_.expGained = false;
            state_.awaitKey = true;
        }
        break;

    case BattlePhase::LEARN_MOVE: {
        Pokemon& pp = pl_.party[state_.learnQueuePoke[state_.learnQueueIdx]];
        int mvId = state_.learnQueue[state_.learnQueueIdx];
        if (state_.learnSubPhase == 0) {
            // 안내 메시지 → A → 잊을 기술 선택
            if (key == Key::A || key == Key::B) {
                state_.learnSubPhase     = 1;
                state_.learnForgetCursor = 0;
            }
        } else if (state_.learnSubPhase == 1) {
            // 잊을 기술 선택 (0~numMoves-1 = 기술, numMoves = 포기)
            int nOpt = pp.numMoves + 1;
            if (key == Key::UP)   state_.learnForgetCursor = (state_.learnForgetCursor - 1 + nOpt) % nOpt;
            if (key == Key::DOWN) state_.learnForgetCursor = (state_.learnForgetCursor + 1) % nOpt;
            if (key == Key::B || (key == Key::A && state_.learnForgetCursor >= pp.numMoves)) {
                // 포기
                swprintf(state_.msg, 128, L"%ls은(는) %ls을(를) 배우지 않았다.",
                    pp.species->name, getMoveData(mvId).name);
                state_.msg2[0] = 0;
                state_.learnSubPhase = 2;
                state_.awaitKey = true;
            } else if (key == Key::A) {
                // 선택한 슬롯의 기술을 새 기술로 교체
                int slot  = state_.learnForgetCursor;
                int oldId = pp.moves[slot].moveId;
                pp.moves[slot].moveId = mvId;
                pp.moves[slot].pp     = getMoveData(mvId).maxPP;
                swprintf(state_.msg, 128, L"%ls은(는) %ls을(를) 잊고",
                    pp.species->name, getMoveData(oldId).name);
                swprintf(state_.msg2, 128, L"새로운 기술 %ls을(를) 배웠다!", getMoveData(mvId).name);
                state_.learnSubPhase = 2;
                state_.awaitKey = true;
            }
        } else {
            // subPhase 2: 결과 메시지 → 다음 큐 또는 종료
            if (key == Key::A || key == Key::B) {
                state_.learnQueueIdx++;
                if (state_.learnQueueIdx < state_.learnQueueCnt) {
                    // 다음 항목 — 빈 슬롯 / 4가득 분기를 다시 처리
                    state_.learnForgetCursor = 0;
                    setupLearnMoveItem();
                } else {
                    // 모든 교체 처리 완료 → 다음 상대 또는 승리
                    state_.learnQueueCnt = 0;
                    state_.learnQueueIdx = 0;
                    advanceAfterFaint();
                }
            }
        }
        break;
    }

    case BattlePhase::VICTORY:
        if (key == Key::A || key == Key::B)
            state_.phase = BattlePhase::DONE;
        break;

    case BattlePhase::DEFEAT:
        if (key == Key::A || key == Key::B)
            state_.phase = BattlePhase::DONE;
        break;

    case BattlePhase::DONE:
        break;

    default: break;
    }
    (void)prevPhase;  // printW가 이제 buffer 사용 → phase 변경 redrawAll 불필요
}

// ─── 스프라이트 그리기 ────────────────────────────────────────
void Battle::drawSprite(int x, int y, int speciesId, bool back) {
    const SpriteData* spr = back ? getSpriteBack(speciesId) : getSpriteFront(speciesId);
    if (!spr && back) spr = getSpriteFront(speciesId);  // 뒷모습 없으면 앞모습으로 대체
    if (!spr) return;
    // sprite마다 actual height (front=20, back=16). nullptr 패딩 만나면 중단.
    for (int r = 0; r < spr->height; r++) {
        if (!spr->rows[r]) break;
        ren_.printRaw(x, y + r, spr->rows[r]);
    }
}

// ─── HP 바 그리기 (pokered 유사: HP: 라벨 + 색 채운 막대) ────
// 멀티바이트 UTF-8 (█/░ = 3 byte)를 setCellU로 1 cell씩 저장.
// ren_.print는 byte 단위라 멀티바이트는 ???로 깨짐.
void Battle::drawHPBar(int x, int y, int cur, int maxHP, const std::string& col) {
    int total = 16;
    int filled = (cur > 0) ? (cur * total / maxHP) : 0;
    if (cur > 0 && filled == 0) filled = 1;  // 1 이상이면 최소 1칸
    // "HP:" 라벨 — ASCII이라 print OK
    static const std::string BG_PAPER_LOCAL = "\033[48;5;253m";
    ren_.print(x, y, "HP:", BG_PAPER_LOCAL + Color::BLACK);
    // 막대 — █(채움) / ░(빈칸) 멀티바이트
    static const std::string BLOCK_FULL  = "\xe2\x96\x88";  // █
    static const std::string BLOCK_EMPTY = "\xe2\x96\x91";  // ░
    std::string emptyCol = BG_PAPER_LOCAL + Color::BRIGHT_BLACK;
    for (int i = 0; i < total; i++) {
        if (i < filled) ren_.setCellU(x + 4 + i, y, BLOCK_FULL,  col);
        else            ren_.setCellU(x + 4 + i, y, BLOCK_EMPTY, emptyCol);
    }
}

// ─── 렌더 ────────────────────────────────────────────────────
// pokered GB 화면 분위기: 회색 배경 + 흰 박스 (HUD, 메시지)
static const std::string BG_GRAY  = "\033[48;5;244m";  // 회색 배경 (sprite 영역)
static const std::string BG_PAPER = "\033[48;5;253m";  // 흰 종이 (박스 안)
static const std::string FG_DARK  = "\033[38;5;232m";  // 검정 글자

void Battle::render() {
    int W = ren_.width;
    int H = ren_.height;

    // ── INTRO_TRANSITION: 검정 → 흰 → 검정 깜빡임 ─────────────
    if (state_.phase == BattlePhase::INTRO_TRANSITION) {
        int f = state_.transitionFrame;
        std::string col;
        if (f > 12)      col = std::string(Color::BG_BLACK) + Color::BLACK;
        else if (f > 6)  col = std::string(Color::BG_WHITE) + Color::WHITE;
        else             col = std::string(Color::BG_BLACK) + Color::BLACK;
        ren_.fillRect(0, 0, W, H, ' ', col);
        return;
    }

    // 배경: 회색 (sprite의 흰 외곽선이 도드라지게)
    ren_.fillRect(0, 0, W, H, ' ', BG_GRAY + Color::BLACK);

    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;

    // ── 가방 화면 (CHOOSE_ITEM) ───────────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_ITEM) {
        int boxH = H - 2, boxW = 40, boxX = (W - 40) / 2, boxY = 1;
        ren_.drawBox(boxX, boxY, boxW, boxH, std::string(Color::BG_BLACK) + Color::WHITE);
        ren_.fillRect(boxX+1, boxY+1, boxW-2, boxH-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        if (state_.itemMode == 0) {
            int slots[MAX_BAG_SLOTS];
            int n = battleBagVisible(slots);
            for (int vi = 0; vi < n; vi++) {
                int i = slots[vi];
                int cy = boxY + 3 + vi * 2;
                bool isCur = (vi == state_.cursor);
                std::string cc = std::string(Color::BG_BLACK) +
                    (isCur ? Color::BRIGHT_YELLOW : Color::WHITE);
                if (isCur) ren_.print(boxX+2, cy, ">", cc);
                char cntStr[16];
                snprintf(cntStr, sizeof(cntStr), "x%d", pl_.bag[i].count);
                ren_.print(boxX + boxW - 8, cy, cntStr, std::string(Color::BG_BLACK) + Color::WHITE);
            }
        } else {
            for (int i = 0; i < pl_.partySize; i++) {
                int cy = boxY + 3 + i * 2;
                bool isCur = (i == state_.cursor);
                std::string cc = std::string(Color::BG_BLACK) +
                    (isCur ? Color::BRIGHT_YELLOW : Color::WHITE);
                if (isCur) ren_.print(boxX+2, cy, ">", cc);
                char hpStr[32];
                snprintf(hpStr, sizeof(hpStr), "HP:%d/%d",
                    pl_.party[i].currentHP, pl_.party[i].maxHP);
                ren_.print(boxX + boxW - 14, cy, hpStr, std::string(Color::BG_BLACK) + Color::WHITE);
            }
        }
        return;
    }

    // ── 포켓몬 교체 화면 ──────────────────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_POKEMON) {
        int boxH = H - 2, boxW = 40, boxX = (W - 40) / 2, boxY = 1;
        ren_.drawBox(boxX, boxY, boxW, boxH, std::string(Color::BG_BLACK) + Color::WHITE);
        ren_.fillRect(boxX+1, boxY+1, boxW-2, boxH-2, ' ', std::string(Color::BG_BLACK) + Color::WHITE);
        for (int i = 0; i < pl_.partySize; i++) {
            int cy = boxY + 2 + i * 2;
            bool isCur = (i == state_.cursor);
            std::string cc = std::string(Color::BG_BLACK) +
                (isCur ? Color::BRIGHT_YELLOW : Color::WHITE);
            if (isCur) ren_.print(boxX+2, cy, ">", cc);
            char hpStr[32];
            snprintf(hpStr, sizeof(hpStr), "  HP:%d/%d",
                pl_.party[i].currentHP, pl_.party[i].maxHP);
            ren_.print(boxX+12, cy, hpStr, std::string(Color::BG_BLACK) + Color::WHITE);
        }
        return;
    }

    // ── 통상 배틀 화면 (pokered layout) ──────────────────────
    //   Enemy HUD: 좌상단    Enemy sprite: 우상단
    //   Player sprite: 좌하단   Player HUD: 우하단
    //   Message box: 하단 전폭
    int boxH = 6;
    int msgY = H - boxH - 1;

    // ── Enemy HUD 박스 (좌상단) ────────────────────────────────
    int enInfoX = 2;
    int enInfoY = 1;
    int enBoxW = 30;
    int enBoxH = 5;
    ren_.drawBox(enInfoX - 1, enInfoY - 1, enBoxW, enBoxH, BG_PAPER + FG_DARK);
    ren_.fillRect(enInfoX, enInfoY, enBoxW - 2, enBoxH - 2, ' ', BG_PAPER + FG_DARK);
    int enHpPct = state_.dispEnemyHP * 100 / (en.maxHP > 0 ? en.maxHP : 1);
    std::string enHpCol = BG_PAPER +
        ((enHpPct > 50) ? Color::GREEN :
         (enHpPct > 20) ? Color::YELLOW : Color::RED);
    drawHPBar(enInfoX, enInfoY + 2, state_.dispEnemyHP, en.maxHP, enHpCol);

    // ── Player HUD 박스 (우하단, msg box 위) ─────────────────
    int myInfoX = W - 32;
    int myInfoY = msgY - 5;
    int myBoxW = 30;
    int myBoxH = 5;
    ren_.drawBox(myInfoX - 1, myInfoY - 1, myBoxW, myBoxH, BG_PAPER + FG_DARK);
    ren_.fillRect(myInfoX, myInfoY, myBoxW - 2, myBoxH - 2, ' ', BG_PAPER + FG_DARK);
    int myHpPct = state_.dispPlayerHP * 100 / (myPoke.maxHP > 0 ? myPoke.maxHP : 1);
    std::string myHpCol = BG_PAPER +
        ((myHpPct > 50) ? Color::GREEN :
         (myHpPct > 20) ? Color::YELLOW : Color::RED);
    drawHPBar(myInfoX, myInfoY + 2, state_.dispPlayerHP, myPoke.maxHP, myHpCol);

    // 대화창 박스 (하단) — 흰 종이 + 검정 테두리
    int boxY = msgY;
    int boxW = W - 4;
    int boxX = 2;
    ren_.drawBox(boxX, boxY, boxW, boxH, BG_PAPER + FG_DARK);
    ren_.fillRect(boxX+1, boxY+1, boxW-2, boxH-2, ' ', BG_PAPER + FG_DARK);

    // 커맨드 메뉴 (CHOOSE_ACTION) — 우측 절반에 작은 박스
    if (state_.phase == BattlePhase::CHOOSE_ACTION) {
        int cmdX = boxX + boxW / 2;
        ren_.drawBox(cmdX, boxY, boxW/2, boxH, BG_PAPER + FG_DARK);
        ren_.fillRect(cmdX+1, boxY+1, boxW/2-2, boxH-2, ' ', BG_PAPER + FG_DARK);
    }

    // 기술 선택 메뉴 (CHOOSE_MOVE)
    if (state_.phase == BattlePhase::CHOOSE_MOVE) {
        for (int i = 0; i < myPoke.numMoves; i++) {
            int cx = boxX + 2 + (i % 2) * 16;
            int cy = boxY + 2 + (i / 2);
            char ppStr[16];
            snprintf(ppStr, sizeof(ppStr), " PP:%d", myPoke.moves[i].pp);
            // 대화창(흰 종이 BG_PAPER) 위에 그리므로 배경을 종이색으로 통일
            std::string cc = BG_PAPER +
                (state_.cursor == i ? Color::RED : FG_DARK);
            if (state_.cursor == i) ren_.print(cx - 2, cy, ">", cc);
            ren_.print(cx + 8, cy, ppStr, BG_PAPER + Color::BRIGHT_BLACK);
        }
        ren_.print(boxX + 2, boxY + boxH - 2, "[ B: back ]",
            BG_PAPER + FG_DARK);
    }
}

void Battle::renderKorean() {
    int W = ren_.width;
    int H = ren_.height;
    int mid = H / 2;

    // INTRO_TRANSITION 중엔 sprite/HUD/텍스트 출력 안 함 (검정/흰 화면만)
    if (state_.phase == BattlePhase::INTRO_TRANSITION) return;

    Pokemon& myPoke = pl_.party[state_.playerPartyIdx];
    Pokemon& en     = state_.enemy;

    // ── 가방 화면 한글 (CHOOSE_ITEM) ──────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_ITEM) {
        int boxX = (W - 40) / 2;
        int boxY = 1;
        if (state_.itemMode == 0) {
            ren_.printW(boxX + 2, boxY + 1, L"가방:",
                std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
            int slots[MAX_BAG_SLOTS];
            int n = battleBagVisible(slots);
            if (n == 0) {
                ren_.printW(boxX + 4, boxY + 4, L"가방이 비어있다.",
                    std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
            } else {
                for (int vi = 0; vi < n; vi++) {
                    int i = slots[vi];
                    int cy = boxY + 3 + vi * 2;
                    std::string cc = std::string(Color::BG_BLACK) +
                        (vi == state_.cursor ? Color::BRIGHT_YELLOW : Color::WHITE);
                    ren_.printW(boxX + 4, cy, getItemName(pl_.bag[i].id), cc);
                }
            }
            ren_.printW(boxX + 2, H - 3, L"[Z]: 사용  [B]: 취소",
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        } else {
            ren_.printW(boxX + 2, boxY + 1, L"상처약: 누구에게?",
                std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
            for (int i = 0; i < pl_.partySize; i++) {
                int cy = boxY + 3 + i * 2;
                std::string cc = std::string(Color::BG_BLACK) +
                    (i == state_.cursor ? Color::BRIGHT_YELLOW : Color::WHITE);
                if (pl_.party[i].species) {
                    ren_.printW(boxX + 4, cy, pl_.party[i].species->name, cc);
                }
            }
            ren_.printW(boxX + 2, H - 3, L"[Z]: 사용  [B]: 뒤로",
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        }
        return;
    }

    // ── 포켓몬 교체 화면 한글 ─────────────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_POKEMON) {
        int boxX = (W - 40) / 2;
        int boxY = 1;
        ren_.printW(boxX + 2, boxY + 1, L"포켓몬 선택:",
            std::string(Color::BG_BLACK) + Color::BRIGHT_YELLOW);
        for (int i = 0; i < pl_.partySize; i++) {
            int cy = boxY + 2 + i * 2;
            std::string cc = std::string(Color::BG_BLACK) +
                (i == state_.cursor ? Color::BRIGHT_YELLOW : Color::WHITE);
            if (pl_.party[i].species) {
                wchar_t buf[32];
                swprintf(buf, 32, L"%ls Lv.%d", pl_.party[i].species->name, pl_.party[i].level);
                ren_.printW(boxX + 4, cy, buf, cc);
            }
        }
        if (state_.switchAfterFaint)
            ren_.printW(boxX + 2, boxY + 14, L"다음 포켓몬을 선택하세요!",
                std::string(Color::BG_BLACK) + Color::BRIGHT_RED);
        else
            ren_.printW(boxX + 2, boxY + 14, L"[ B: 취소 ]",
                std::string(Color::BG_BLACK) + Color::BRIGHT_BLACK);
        return;
    }

    wchar_t buf[64];

    // pokered layout: enemy 좌상단 HUD + 우상단 sprite, player 좌하단 sprite + 우하단 HUD
    int boxH = 6;
    int boxY = H - boxH - 1;
    int boxX = 2;

    // ── 트레이너 인트로 판정 ───────────────────────────────────
    // 첫 SHOW_MSG (turnStarted=false, !trainerIntroShown)에만 풀바디 표시.
    // 이후 게임 진행 중 SHOW_MSG에선 적 포켓몬 sprite로 교체.
    bool trainerIntro =
        (state_.type == BattleType::TRAINER || state_.type == BattleType::BOSS) &&
        !state_.trainerIntroShown &&
        !state_.turnStarted &&
        state_.phase == BattlePhase::SHOW_MSG;

    // ── Enemy sprite (우상단) ──────────────────────────────────
    const SpriteData* enSpr = getSpriteFront(en.species->id);
    int enW = enSpr ? enSpr->width  : 40;
    int enH = enSpr ? enSpr->height : 20;
    int enSprX = W - enW - 2;
    int enSprY = 1;
    if (trainerIntro && state_.trainerName) {
        const IntroSprite* trSpr = &SPR_INTRO_OAK;
        switch (state_.trainerIntroId) {
            case 1: trSpr = &SPR_INTRO_RIVAL; break;
            case 2: trSpr = &SPR_INTRO_RED; break;
            case 3: trSpr = &SPR_INTRO_BROCK; break;
            case 4: trSpr = &SPR_INTRO_BUG_CATCHER; break;
            case 5: trSpr = &SPR_INTRO_COOLTRAINER_M; break;
            default: trSpr = &SPR_INTRO_OAK; break;
        }
        // 라이벌 이름 매칭 (기존 동작 호환 — 인트로 ID 0이면서 이름이 라이벌이면 RIVAL)
        if (state_.trainerIntroId == 0 && wcscmp(state_.trainerName, pl_.rivalName) == 0)
            trSpr = &SPR_INTRO_RIVAL;
        int spX = W - INTRO_SPR_W - 2;
        if (spX < 0) spX = 0;
        for (int i = 0; i < INTRO_SPR_H && enSprY + i < boxY - 1; i++) {
            if (trSpr->rows[i])
                ren_.printRaw(spX, enSprY + i, trSpr->rows[i]);
        }
    } else {
        drawSprite(enSprX, enSprY, en.species->id, false);
    }

    // ── Player sprite (좌하단, msg box 위) ────────────────────
    const SpriteData* mySpr = getSpriteBack(myPoke.species->id);
    if (!mySpr) mySpr = getSpriteFront(myPoke.species->id);
    int myW = mySpr ? mySpr->width  : 32;
    int myH = mySpr ? mySpr->height : 16;
    int mySprX = 2;
    int mySprY = boxY - myH - 1;
    if (mySprY < enSprY + enH + 1) mySprY = enSprY + enH + 1;  // 적 sprite 겹침 방지
    drawSprite(mySprX, mySprY, myPoke.species->id, true);

    // ── Enemy HUD (좌상단, 흰 박스 안) — 이름/Lv/HP ───────────
    int enInfoX = 2;
    int enInfoY = 1;
    swprintf(buf, 64, L"상대 %ls  Lv.%d", en.species->name, en.level);
    ren_.printW(enInfoX, enInfoY, buf, BG_PAPER + Color::BLACK);
    swprintf(buf, 64, L"HP: %d / %d", state_.dispEnemyHP, en.maxHP);
    ren_.printW(enInfoX, enInfoY + 1, buf, BG_PAPER + Color::BLACK);

    // ── Player HUD (우하단, 흰 박스 안) — 이름/Lv/HP ──────────
    int myInfoX = W - 32;
    int myInfoY = boxY - 5;
    swprintf(buf, 64, L"%ls  Lv.%d", myPoke.species->name, myPoke.level);
    ren_.printW(myInfoX, myInfoY, buf, BG_PAPER + Color::BLUE);
    swprintf(buf, 64, L"HP: %d / %d", state_.dispPlayerHP, myPoke.maxHP);
    ren_.printW(myInfoX, myInfoY + 1, buf, BG_PAPER + Color::BLACK);

    if (state_.phase == BattlePhase::SHOW_MSG ||
        state_.phase == BattlePhase::FAINT_ENEMY ||
        state_.phase == BattlePhase::FAINT_PLAYER ||
        state_.phase == BattlePhase::LEVEL_UP_MSG ||
        state_.phase == BattlePhase::VICTORY ||
        state_.phase == BattlePhase::DEFEAT ||
        state_.phase == BattlePhase::EXP_OTHERS ||
        (state_.phase == BattlePhase::LEARN_MOVE && state_.learnSubPhase != 1)) {
        ren_.printW(boxX + 2, boxY + 2, state_.msg, BG_PAPER + Color::BLACK);
        if (state_.msg2[0])
            ren_.printW(boxX + 2, boxY + 3, state_.msg2, BG_PAPER + Color::RED);
        if (state_.awaitKey)
            ren_.printW(boxX + 2, boxY + boxH - 2, L"[ Z / Enter: 계속 ]",
                BG_PAPER + Color::BRIGHT_BLACK);
    }

    // ── 기술 교체 선택 (LEARN_MOVE, subPhase 1) ────────────────
    if (state_.phase == BattlePhase::LEARN_MOVE && state_.learnSubPhase == 1) {
        Pokemon& lp = pl_.party[state_.learnQueuePoke[state_.learnQueueIdx]];
        int newId = state_.learnQueue[state_.learnQueueIdx];
        swprintf(buf, 64, L"%ls의 잊을 기술:", getMoveData(newId).name);
        ren_.printW(boxX + 2, boxY + 1, buf, BG_PAPER + Color::BLACK);
        for (int i = 0; i < lp.numMoves; i++) {
            int cx = boxX + 4 + (i % 2) * 16;
            int cy = boxY + 2 + (i / 2);
            const MoveData& mv = getMoveData(lp.moves[i].moveId);
            std::string cc = BG_PAPER +
                (state_.learnForgetCursor == i ? Color::RED : Color::BLACK);
            if (state_.learnForgetCursor == i) ren_.print(cx - 2, cy, ">", cc);
            ren_.printW(cx, cy, mv.name, cc);
        }
        // 포기 옵션 (index = numMoves)
        int gi = lp.numMoves;
        int gcx = boxX + 4 + (gi % 2) * 16;
        int gcy = boxY + 2 + (gi / 2);
        std::string gcc = BG_PAPER +
            (state_.learnForgetCursor == gi ? Color::RED : Color::BLACK);
        if (state_.learnForgetCursor == gi) ren_.print(gcx - 2, gcy, ">", gcc);
        ren_.printW(gcx, gcy, L"배우지 않기", gcc);
    }

    // ── 기술 이름 (CHOOSE_MOVE) ───────────────────────────────
    if (state_.phase == BattlePhase::CHOOSE_MOVE) {
        ren_.printW(boxX + 2, boxY + 1, L"기술 선택:",
            BG_PAPER + Color::BLACK);
        for (int i = 0; i < myPoke.numMoves; i++) {
            int cx = boxX + 2 + (i % 2) * 16;
            int cy = boxY + 2 + (i / 2);
            const MoveData& mv = getMoveData(myPoke.moves[i].moveId);
            std::string cc = BG_PAPER +
                (state_.cursor == i ? Color::RED : Color::BLACK);
            ren_.printW(cx, cy, mv.name, cc);
        }
    }

    // ── 커맨드 헤더 + 4개 한국어 메뉴 (CHOOSE_ACTION) ─────────
    if (state_.phase == BattlePhase::CHOOSE_ACTION) {
        swprintf(buf, 64, L"%ls은(는) 어떻게 할까?", myPoke.species->name);
        ren_.printW(boxX + 2, boxY + 1, buf, BG_PAPER + Color::BLACK);
        int cmdX = boxX + (W - 4) / 2;
        const wchar_t* cmds[4] = {L"싸운다", L"가방", L"포켓몬", L"도망"};
        for (int i = 0; i < 4; i++) {
            int cx = cmdX + 2 + (i % 2) * 10;
            int cy = boxY + 2 + (i / 2);
            std::string cc = BG_PAPER +
                (state_.cursor == i ? Color::RED : Color::BLACK);
            if (state_.cursor == i)
                ren_.print(cx - 2, cy, ">", cc);
            ren_.printW(cx, cy, cmds[i], cc);
        }
    }
}
