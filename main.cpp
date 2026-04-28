#include <iostream>
#include <cstdlib>
#include <ctime>
#include "main.h"
#include "UltimateBoard.h"
#include "RandomPlayer.h"

// --- Fonctions de conversion ---

// Convertit notre format (Macro/Micro) vers le format de la lib du prof (Row/Col 0-8)
GameMove convertToGameMove(const Move& m) {
    return {m.macroR * 3 + m.microR, m.macroC * 3 + m.microC};
}

// Convertit le format de la lib du prof (Row/Col 0-8) vers notre format (Macro/Micro)
Move convertToMyMove(const GameMove& gm) {
    return Move(gm.row / 3, gm.col / 3, gm.row % 3, gm.col % 3);
}

// --- Fonction principale ---

int main()
{
    // Initialisation de l'aléatoire pour notre IA Random
    std::srand(std::time(nullptr));

    // Game initialization : 10 parties, niveau Facile 1, mode Debug, on joue en second (false)
    game.initialize(10, Level::EASY_1, Mode::DEBUG, false, "Equipe_A3_Random");

    // Création de notre IA (on utilise les 'O' car l'IA adverse joue en premier avec les 'X')
    RandomPlayer monIA("RandomBot", Symbol::O);

    while (!game.isAllGameFinish())
    {
        // À chaque nouvelle partie, on instancie un plateau vierge en mémoire
        UltimateBoard internalBoard;

        while (!game.isFinish())
        {
            // 1. Récupération du coup adverse depuis la librairie
            GameMove opponentMoveRaw;
            game.getMove(opponentMoveRaw);

            std::cerr << "Coup recu de l'IA adverse : [" << opponentMoveRaw.row << ", " << opponentMoveRaw.col << "]" << std::endl;

            // On vérifie que le coup de l'adversaire est valide (>= 0).
            // C'est une sécurité au cas où la librairie envoie des coordonnées négatives au premier tour si vous deviez jouer en premier.
            if (opponentMoveRaw.row >= 0 && opponentMoveRaw.col >= 0) {
                Move opponentMove = convertToMyMove(opponentMoveRaw);
                // L'IA adverse utilise toujours le symbole X lorsqu'elle joue en premier
                internalBoard.makeMove(opponentMove, Symbol::X);
            }

            // 2. Notre IA analyse notre plateau interne mis à jour et décide d'un coup
            Move myMove = monIA.chooseMove(internalBoard);

            // Sécurité : si notre IA ne trouve aucun coup, on évite un crash
            if (myMove.macroR == -1) {
                std::cerr << "Erreur : Aucun coup valide trouve par l'IA interne !" << std::endl;
                break;
            }

            // 3. On enregistre notre propre coup sur notre simulateur interne
            internalBoard.makeMove(myMove, monIA.getSymbol());

            // 4. On convertit notre coup et on le transmet à la librairie du prof
            GameMove myMoveRaw = convertToGameMove(myMove);
            std::cerr << "Coup envoye par notre IA : [" << myMoveRaw.row << ", " << myMoveRaw.col << "]" << std::endl;

            game.setMove(myMoveRaw);
        }

        std::cerr << "--- Fin d'une partie ---" << std::endl;
    }

    std::cerr << "--- Fin du match ---" << std::endl;
    return 0;
}
