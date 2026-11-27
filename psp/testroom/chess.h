#ifndef DMC3_CHESS_H
#define DMC3_CHESS_H

/* CEm017Master (43f160). Actor slots stay stable for an encounter. EST ids35..40
 * are Pawn, Knight, Bishop, Rook, Queen, King. No square occupancy/grid exists
 * in this scheduler: each piece owns its continuous movement and floor probes.
 * Fill snapshots once per update, call chess_update, then consume commands.
 * waiting/busy map +110 bits20000000/40000000; hurt maps bit2 (HP fell).
 * command maps +488: 10 normal,15 alternate,20 Queen response,30 castle.
 * swap_ready maps +3bc bit20000000; swap_blocked maps +110 bit08000000.
 * alternate_ready maps +3bc bit40000000, probability is40ab60's rank table.
 * A consumed command must be cleared by its actor, not by this controller. */
typedef struct ChessPiece {
    int active, est, waiting, busy, hurt, alternate_ready, swap_ready, swap_blocked;
    int command, kill; /* king gone: native +3bc bit8000, one actor per30 updates */
    float waited, alternate_percent, pos[3], destination[3];
} ChessPiece;
typedef struct { int king_seen, death_clock, recent_hurt, capacity, count, busy; } ChessMaster;
void chess_reset(ChessMaster *master);
/* difficulty is the game's dynamic difficulty value (700/1000 boundaries).
 * Counter fields advance per native update, as in43f160, not by animation time. */
void chess_update(ChessMaster *master, ChessPiece *pieces, int count,
                  float difficulty, float (*unit)(void));
float chess_wait_step(float distance, int accelerated);
float chess_wait(int est,int state,int rank,float unit);
float chess_alternate_chance(int est,int rank);
/* enemy.c adapter: the currently selected/updated enemy's stable snapshot.
 * Initialize est at spawn; update waiting/busy/waited from the piece brain.
 * Consume command by clearing it, and use destination for command30. */
ChessPiece *enemy_chess_actor(void);
#endif
