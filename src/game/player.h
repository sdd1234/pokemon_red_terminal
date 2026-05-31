#pragma once
#include "../data/pokemon_data.h"
#include "../data/type_chart.h"
#include <cstring>

struct PokemonMove {
    int moveId;
    int pp;
};

struct Pokemon {
    const PokemonSpecies* species; // nullptr = 빈 슬롯
    int  level;
    int  currentHP, maxHP;
    int  atk, def, spe, spc;
    PokemonMove moves[4];
    int  numMoves;
    int  exp;
    int  atkStage, defStage, speStage, accStage; // 스테이지 -6~+6
};

// ─── 아이템 시스템 (단일 배열, pokered 방식) ────────────────
enum ItemId {
    ITEM_NONE       = 0,
    ITEM_POTION     = 1,  // 상처약 — HP 20 회복
    ITEM_POKE_BALL  = 2,  // 몬스터볼 — 야생 포획
    ITEM_RARE_CANDY = 3,  // 이상한사탕 — 오버월드 전용, 사용 시 레벨 +1 (전투 가방엔 안 보임)
};

struct ItemSlot {
    ItemId id;
    int    count;
};

static constexpr int MAX_BAG_SLOTS = 20;
static constexpr int POTION_HEAL_AMOUNT = 20;

struct Player {
    wchar_t name[16];
    wchar_t rivalName[16];
    Pokemon party[6];
    int     partySize;

    // 아이템 (단일 배열 — id별 슬롯 1개씩만 사용)
    ItemSlot bag[MAX_BAG_SLOTS];
    int      bagSize;
    int      money;
    bool     hasPokedex;

    bool beatenRival1;
    bool gotParcel;       // 상록 마트에서 소포 수령
    bool deliveredParcel; // 오박사에게 소포 전달
    bool beatenGymTrainer1;
    bool beatenGymTrainer2;
    bool beatenBrock;
    int  starterIdx;      // 선택한 스타터 인덱스 (0=이상해씨 1=파이리 2=꼬부기, -1=미선택)
    bool rivalLabTalked;  // beatenRival1 후 연구소 블루와 대화 완료

    int mapId;
    int x, y;
    int dir; // 0=아래 1=위 2=왼 3=오른

    bool viridianHealed;
    bool pewterHealed;
    bool justWokeUp;

    // 디버그 치트(Ctrl+M)로 한 번이라도 워프 메뉴를 연 적 있는지.
    // 첫 진입 시 가방에 모든 아이템을 99개씩 채워 넣음 — 두 번째부터는 추가하지 않음.
    bool cheatItemsGranted;

    // 전멸 시 돌아갈 위치 (마지막 회복 지점) — 원본 wLastBlackoutMap 대응
    int  lastBlackoutMapId;
    int  lastBlackoutX, lastBlackoutY;
    int  lastBlackoutDir;

    // 풀베기로 베어낸 나무 위치 — 게임 재시작 시 리셋
    static constexpr int MAX_CUT_TREES = 16;
    int  cutTreeMapId[MAX_CUT_TREES];
    int  cutTreeX[MAX_CUT_TREES];
    int  cutTreeY[MAX_CUT_TREES];
    int  cutTreeCount;

    // 획득 완료한 아이템볼(필드 포켓볼) 위치 — 획득 후 숨김 처리
    static constexpr int MAX_ITEM_BALLS = 32;
    int  itemBallMapId[MAX_ITEM_BALLS];
    int  itemBallX[MAX_ITEM_BALLS];
    int  itemBallY[MAX_ITEM_BALLS];
    int  itemBallCount;

    // 포켓덱스 — 종족별 포획/처치 횟수 (SPECIES 인덱스 기준). 둘 중 하나라도 >0이면 도감 등록.
    int  dexCaught[NUM_SPECIES_DATA];
    int  dexDefeated[NUM_SPECIES_DATA];
};

// 종족 id → SPECIES 배열 인덱스 (없으면 -1)
inline int speciesIndex(int id) {
    for (int i = 0; i < NUM_SPECIES_DATA; i++)
        if (SPECIES[i].id == id) return i;
    return -1;
}

static constexpr int MOVE_CUT = 18;

inline bool playerHasCut(const Player& pl) {
    for (int i = 0; i < pl.partySize; i++) {
        const Pokemon& p = pl.party[i];
        if (!p.species) continue;
        for (int j = 0; j < p.numMoves; j++) {
            if (p.moves[j].moveId == MOVE_CUT) return true;
        }
    }
    return false;
}

inline bool isTreeCut(const Player& pl, int mapId, int x, int y) {
    for (int i = 0; i < pl.cutTreeCount; i++) {
        if (pl.cutTreeMapId[i] == mapId && pl.cutTreeX[i] == x && pl.cutTreeY[i] == y)
            return true;
    }
    return false;
}

inline bool addCutTree(Player& pl, int mapId, int x, int y) {
    if (pl.cutTreeCount >= Player::MAX_CUT_TREES) return false;
    pl.cutTreeMapId[pl.cutTreeCount] = mapId;
    pl.cutTreeX[pl.cutTreeCount] = x;
    pl.cutTreeY[pl.cutTreeCount] = y;
    pl.cutTreeCount++;
    return true;
}

inline bool isItemBallTaken(const Player& pl, int mapId, int x, int y) {
    for (int i = 0; i < pl.itemBallCount; i++) {
        if (pl.itemBallMapId[i] == mapId && pl.itemBallX[i] == x && pl.itemBallY[i] == y)
            return true;
    }
    return false;
}

inline bool addItemBall(Player& pl, int mapId, int x, int y) {
    if (pl.itemBallCount >= Player::MAX_ITEM_BALLS) return false;
    pl.itemBallMapId[pl.itemBallCount] = mapId;
    pl.itemBallX[pl.itemBallCount] = x;
    pl.itemBallY[pl.itemBallCount] = y;
    pl.itemBallCount++;
    return true;
}

inline void recalcStats(Pokemon& p) {
    if (!p.species) return;
    p.maxHP  = calcHP  (p.species->baseHP,  p.level);
    p.atk    = calcStat(p.species->baseAtk, p.level);
    p.def    = calcStat(p.species->baseDef, p.level);
    p.spe    = calcStat(p.species->baseSpe, p.level);
    p.spc    = calcStat(p.species->baseSpc, p.level);
}

inline bool pokemonHasMove(const Pokemon& p, int moveId) {
    for (int i = 0; i < p.numMoves; i++) {
        if (p.moves[i].moveId == moveId) return true;
    }
    return false;
}

inline bool moveIsStrongAgainstSelf(const Pokemon& p, int moveId) {
    if (!p.species) return false;
    const MoveData& mv = getMoveData(moveId);
    return getEffInt(mv.type, p.species->type1, p.species->type2) >= 20;
}

inline int pickFallbackMove(const Pokemon& p, int excludeMid = 0) {
    if (!p.species) return 0;
    for (int i = 1; i < NUM_MOVES_DATA; i++) {
        int mid = MOVES[i].id;
        if (mid <= 0 || mid == excludeMid) continue;
        if (pokemonHasMove(p, mid)) continue;
        if (getEffInt(MOVES[i].type, p.species->type1, p.species->type2) >= 20) continue;
        return mid;
    }
    return 0;
}

// 이상한사탕 — 레벨 +1 (스탯 재계산, HP 증가분 반영, 3의 배수면 learnset 순서대로 새 기술 후보 탐색).
// 반환:
//    -1 = Lv100이라 못 올림.
//     0 = 레벨업만 하고 학습 안 함 (3의 배수가 아니거나 새 기술 후보 없음).
//   > 0 = 빈 슬롯에 자동으로 추가한 새 기술 id.
// outWantedMid (nullptr 가능): 3의 배수인데 슬롯 4개 가득 차서 학습 못 한 경우, 후보 mid를 저장.
//                              (자동 추가는 안 함 — 호출자가 잊을 기술 선택 UI를 띄워야 함)
inline int rareCandyLevelUp(Pokemon& p, int* outWantedMid = nullptr) {
    if (outWantedMid) *outWantedMid = 0;
    if (!p.species || p.level >= 100) return -1;
    int oldMax = p.maxHP;
    p.level++;
    p.exp = expForLevel(p.level);
    recalcStats(p);
    p.currentHP += (p.maxHP - oldMax);
    if (p.currentHP > p.maxHP) p.currentHP = p.maxHP;
    if (p.currentHP < 1) p.currentHP = 1;
    if (p.level % 3 != 0) return 0;
    // 다음 배울 기술 후보 탐색
    for (int i = 0; i < 8; i++) {
        const LearnMove& lm = p.species->learnset[i];
        if (lm.level == 0 && lm.moveId == 0) break;
        int mid = lm.moveId;
        if (mid == 0) continue;
        if (pokemonHasMove(p, mid)) continue;
        if (moveIsStrongAgainstSelf(p, mid)) continue;
        if (p.numMoves < 4) {
            // 빈 슬롯 — 자동 학습
            p.moves[p.numMoves].moveId = mid;
            p.moves[p.numMoves].pp     = getMoveData(mid).maxPP;
            p.numMoves++;
            return mid;
        }
        // 4 가득 — UI로 위임
        if (outWantedMid) *outWantedMid = mid;
        return 0;
    }
    int mid = pickFallbackMove(p);
    if (mid == 0) return 0;
    if (p.numMoves < 4) {
        p.moves[p.numMoves].moveId = mid;
        p.moves[p.numMoves].pp     = getMoveData(mid).maxPP;
        p.numMoves++;
        return mid;
    }
    if (outWantedMid) *outWantedMid = mid;
    return 0;
}

inline Pokemon makePokemon(int speciesId, int level) {
    Pokemon p = {};
    p.species = getSpecies(speciesId);
    if (!p.species) return p;
    p.level   = level;
    recalcStats(p);
    p.currentHP = p.maxHP;
    p.exp = expForLevel(level);
    p.atkStage = p.defStage = p.speStage = p.accStage = 0;

    p.numMoves = 0;
    for (int i = 0; i < 4 && p.species->startMoves[i] != 0; i++) {
        int mid = p.species->startMoves[i];
        p.moves[p.numMoves].moveId = mid;
        p.moves[p.numMoves].pp     = getMoveData(mid).maxPP;
        p.numMoves++;
    }
    for (int i = 0; i < 8; i++) {
        const LearnMove& lm = p.species->learnset[i];
        if (lm.level == 0) break;
        if (lm.level <= level && p.numMoves < 4) {
            bool exists = false;
            for (int j = 0; j < p.numMoves; j++)
                if (p.moves[j].moveId == lm.moveId) { exists = true; break; }
            if (!exists) {
                p.moves[p.numMoves].moveId = lm.moveId;
                p.moves[p.numMoves].pp     = getMoveData(lm.moveId).maxPP;
                p.numMoves++;
            }
        }
    }
    return p;
}

inline bool pokemonFainted(const Pokemon& p) {
    return p.species && p.currentHP <= 0;
}

inline bool allFainted(const Player& pl) {
    for (int i = 0; i < pl.partySize; i++)
        if (!pokemonFainted(pl.party[i])) return false;
    return pl.partySize > 0;
}

inline int firstAlive(const Player& pl) {
    for (int i = 0; i < pl.partySize; i++)
        if (!pokemonFainted(pl.party[i])) return i;
    return -1;
}

// ─── 상점(Mart) 인벤토리 테이블 ─────────────────────────────
enum MartId {
    MART_NONE     = 0,
    MART_VIRIDIAN = 1,  // 상록시티 마트
    MART_PEWTER   = 2,  // 회색시티 마트
};

struct MartDef {
    int     id;
    const wchar_t* name;
    ItemId  items[4];
    int     numItems;
};

static const MartDef ALL_MARTS[] = {
    {MART_VIRIDIAN, L"상록시티 프렌들리숍",
     {ITEM_POKE_BALL, ITEM_POTION}, 2},
    {MART_PEWTER,   L"회색시티 프렌들리숍",
     {ITEM_POKE_BALL, ITEM_POTION}, 2},
};
static constexpr int NUM_MARTS = sizeof(ALL_MARTS) / sizeof(ALL_MARTS[0]);

inline const MartDef* findMart(int martId) {
    for (int i = 0; i < NUM_MARTS; i++)
        if (ALL_MARTS[i].id == martId) return &ALL_MARTS[i];
    return nullptr;
}

// ─── 아이템 헬퍼 ────────────────────────────────────────────
inline const wchar_t* getItemName(ItemId id) {
    switch (id) {
        case ITEM_POTION:     return L"상처약";
        case ITEM_POKE_BALL:  return L"몬스터볼";
        case ITEM_RARE_CANDY: return L"이상한사탕";
        default:              return L"-";
    }
}

inline int getItemPrice(ItemId id) {
    switch (id) {
        case ITEM_POTION:     return 300;
        case ITEM_POKE_BALL:  return 200;
        case ITEM_RARE_CANDY: return 0;   // 상점 미판매 (필드 입수 전용)
        default:              return 0;
    }
}

inline int findBagSlot(const Player& pl, ItemId id) {
    for (int i = 0; i < pl.bagSize; i++)
        if (pl.bag[i].id == id) return i;
    return -1;
}

inline int getItemCount(const Player& pl, ItemId id) {
    int idx = findBagSlot(pl, id);
    return idx >= 0 ? pl.bag[idx].count : 0;
}

inline bool addItem(Player& pl, ItemId id, int count) {
    if (count <= 0 || id == ITEM_NONE) return false;
    int idx = findBagSlot(pl, id);
    if (idx >= 0) {
        pl.bag[idx].count += count;
        return true;
    }
    if (pl.bagSize >= MAX_BAG_SLOTS) return false;
    pl.bag[pl.bagSize].id    = id;
    pl.bag[pl.bagSize].count = count;
    pl.bagSize++;
    return true;
}

inline bool removeItem(Player& pl, ItemId id, int count) {
    int idx = findBagSlot(pl, id);
    if (idx < 0 || pl.bag[idx].count < count) return false;
    pl.bag[idx].count -= count;
    if (pl.bag[idx].count == 0) {
        for (int i = idx; i < pl.bagSize - 1; i++)
            pl.bag[i] = pl.bag[i+1];
        pl.bagSize--;
    }
    return true;
}

inline void recordBlackoutPoint(Player& pl) {
    pl.lastBlackoutMapId = pl.mapId;
    pl.lastBlackoutX     = pl.x;
    pl.lastBlackoutY     = pl.y;
    pl.lastBlackoutDir   = pl.dir;
}

inline void healAll(Player& pl) {
    for (int i = 0; i < pl.partySize; i++) {
        Pokemon& p = pl.party[i];
        if (!p.species) continue;
        p.currentHP = p.maxHP;
        for (int j = 0; j < p.numMoves; j++)
            p.moves[j].pp = getMoveData(p.moves[j].moveId).maxPP;
        p.atkStage = p.defStage = p.speStage = p.accStage = 0;
    }
}
