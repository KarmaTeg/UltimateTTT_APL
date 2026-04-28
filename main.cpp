#include <iostream>
#include <cstdlib>
#include <ctime>
#include "main.h"
#include "UltimateBoard.h"
#include "RandomPlayer.h"

// --- Fonctions de conversion ---

GameMove convertToGameMove(const Move& m) {
    return {m.macroR * 3 + m.microR, m.macroC * 3 + m.microC};
}

Move convertToMyMove(const GameMove& gm) {
    return Move(gm.row / 3, gm.col / 3, gm.row % 3, gm.col % 3);
}

// --- Fonction principale ---

int main()
{
    std::srand(std::time(nullptr));

    // IMPORTANT : On définit ici si on joue en premier ou en second.
    // false = L'IA du prof joue en premier. true = Notre IA joue en premier.
    bool wePlayFirst = false;

    game.initialize(10, Level::EASY_1, Mode::DEBUG, wePlayFirst, "Equipe_A3_Random");

    // Création de notre IA (on prend 'O' si on joue en second, 'X' si on joue en premier)
    Symbol mySymbol = wePlayFirst ? Symbol::X : Symbol::O;
    Symbol oppSymbol = wePlayFirst ? Symbol::O : Symbol::X;
    RandomPlayer monIA("RandomBot", mySymbol);

    while (!game.isAllGameFinish())
    {
        UltimateBoard internalBoard;

        // On initialise le tour actuel en fonction des paramètres de départ
        bool myTurn = wePlayFirst;

        while (!game.isFinish())
        {
            if (myTurn)
            {
                // --- C'EST NOTRE TOUR ---
                Move myMove = monIA.chooseMove(internalBoard);

                if (myMove.macroR != -1) { // Sécurité si un coup valide a été trouvé
                    internalBoard.makeMove(myMove, mySymbol); // Mise à jour de notre mémoire

                    GameMove myMoveRaw = convertToGameMove(myMove);
                    std::cerr << "Coup envoye par notre IA : [" << myMoveRaw.row << ", " << myMoveRaw.col << "]" << std::endl;

                    game.setMove(myMoveRaw); // On envoie le coup à la librairie
                }

                myTurn = false; // Fin de notre tour, on passe la main
            }
            else
            {
                // --- C'EST LE TOUR DE L'ADVERSAIRE ---
                GameMove opponentMoveRaw;

                // On écoute la librairie. Le 'if' bloque tant que l'adversaire n'a pas joué
                if (game.getMove(opponentMoveRaw))
                {
                    std::cerr << "Coup recu de l'IA adverse : [" << opponentMoveRaw.row << ", " << opponentMoveRaw.col << "]" << std::endl;

                    Move opponentMove = convertToMyMove(opponentMoveRaw);
                    internalBoard.makeMove(opponentMove, oppSymbol); // Mise à jour de notre mémoire

                    myTurn = true; // L'adversaire a joué, c'est à nous !
                }
            }
        }
        std::cerr << "--- Fin d'une partie ---" << std::endl;
    }

    std::cerr << "--- Fin du match complet ---" << std::endl;
    return 0;
}
