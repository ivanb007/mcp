// main_st.cpp — entry for the single-thread engine build

#include "uci.h"

int main() {
    runUciLoop();  // single-threaded UCI loop from uci_st.cpp
    return 0;
}
