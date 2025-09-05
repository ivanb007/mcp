#pragma once
#include "heuristics.h"

struct RootAggregate {
    HistoryTable history;
    KillerTable  killers;
    TransTable   tt;

    RootAggregate(size_t ttSize = (1u<<20)) : history(), killers(), tt(ttSize) {}
    void mergeFrom(const HistoryTable& h) { history.mergeFrom(h); }
    void mergeFrom(const KillerTable& k)  { killers.mergeFrom(k); }
    void mergeFrom(const TransTable& t)   { tt.mergeFrom(t); }
};
