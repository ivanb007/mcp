#pragma once
#include "heuristics.h"

struct ThreadContext {
    EvalMatrix   eval;
    HistoryTable history;
    KillerTable  killers;
    TransTable   tt;
    uint16_t     age = 0;

    ThreadContext(size_t ttSize = (1u<<20)) : eval(), history(), killers(), tt(ttSize) {}
    
    void clearPlyData() { killers.clear(); }
    void resetAll() { eval.clear();  history.clear(); killers.clear(); tt.clear(); }
};

extern thread_local ThreadContext g_ctx; // one per thread
