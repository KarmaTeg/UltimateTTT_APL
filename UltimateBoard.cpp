#include "UltimateBoard.h"

UltimateBoard::UltimateBoard() : nextMacroR(-1), nextMacroC(-1), globalWinner(Symbol::NONE) {}

bool UltimateBoard::isValidMove(const Move& m) const {
    // Vérifier les limites
    if (m.macroR < 0 || m.macroR > 2 || m.macroC < 0 || m.macroC > 2) return false;

    // Si une grille spécifique est imposée, on doit y jouer
    if (nextMacroR != -1 && (m.macroR != nextMacroR || m.macroC != nextMacroC)) return false;

    // La grille cible ne doit pas être terminée
    if (grids[m.macroR][m.macroC].getWinner() != Symbol::NONE) return false;

    // La case doit être vide
    return grids[m.macroR][m.macroC].getCell(m.microR, m.microC) == Symbol::NONE;
}

bool UltimateBoard::makeMove(const Move& m, Symbol s) {
    if (!isValidMove(m)) return false;

    // Jouer le coup dans la sous-grille correspondante
    grids[m.macroR][m.macroC].play(m.microR, m.microC, s);

    // Déterminer la prochaine grille active en fonction de la case jouée
    if (grids[m.microR][m.microC].getWinner() == Symbol::NONE) {
        nextMacroR = m.microR;
        nextMacroC = m.microC;
    } else {
        // La grille cible est finie -> Coup libre sur tout le plateau
        nextMacroR = -1;
        nextMacroC = -1;
    }

    checkGlobalWinner();
    return true;
}

std::vector<Move> UltimateBoard::getValidMoves() const {
    std::vector<Move> moves;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            // Ignorer les grilles qui ne sont pas la grille active (sauf si coup libre)
            if (nextMacroR != -1 && (r != nextMacroR || c != nextMacroC)) continue;
            // Ignorer les grilles terminées
            if (grids[r][c].getWinner() != Symbol::NONE) continue;

            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    if (grids[r][c].getCell(i, j) == Symbol::NONE) {
                        moves.emplace_back(r, c, i, j);
                    }
                }
            }
        }
    }
    return moves;
}

Symbol UltimateBoard::checkGlobalWinner() {
    Symbol winners[3][3];
    for(int i=0; i<3; ++i)
        for(int j=0; j<3; ++j)
            winners[i][j] = grids[i][j].getWinner();

    for (int i = 0; i < 3; i++) {
        if (winners[i][0] != Symbol::NONE && winners[i][0] != Symbol::DRAW && winners[i][0] == winners[i][1] && winners[i][1] == winners[i][2])
            globalWinner = winners[i][0];
        if (winners[0][i] != Symbol::NONE && winners[0][i] != Symbol::DRAW && winners[0][i] == winners[1][i] && winners[1][i] == winners[2][i])
            globalWinner = winners[0][i];
    }

    if (winners[0][0] != Symbol::NONE && winners[0][0] != Symbol::DRAW && winners[0][0] == winners[1][1] && winners[1][1] == winners[2][2])
        globalWinner = winners[0][0];
    if (winners[0][2] != Symbol::NONE && winners[0][2] != Symbol::DRAW && winners[0][2] == winners[1][1] && winners[1][1] == winners[2][0])
        globalWinner = winners[0][2];

    return globalWinner;
}
