#ifndef MOVE_H
#define MOVE_H

struct Move {
    int macroR, macroC; // Position de la petite grille (0-2)
    int microR, microC; // Position dans la petite grille (0-2)

    Move(int maR = -1, int maC = -1, int miR = -1, int miC = -1)
        : macroR(maR), macroC(maC), microR(miR), microC(miC) {}
};

#endif // MOVE_H
