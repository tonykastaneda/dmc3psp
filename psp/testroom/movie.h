#ifndef DMC3PSP_TESTROOM_MOVIE_H
#define DMC3PSP_TESTROOM_MOVIE_H

/* Cutscene playback: JPEG frames through the PSP's hardware decoder with IMA ADPCM sound, from the
 * files tools/prepare_movies.py makes. Blocks until the movie ends or START or CROSS is pressed.
 * Returns 1 if it played, 0 if it couldn't (no file, no decoder), in which case the game just goes on.
 * draw_buffer is the buffer being drawn, updated across the movie's frames. stop_after > 0 ends it
 * after that many frames, and on_frame runs after each one; tests use them. */
typedef void (*MovieFrame)(int frame);
int movie_play(const char *path, void **draw_buffer, int stop_after, MovieFrame on_frame);
/* What the last movie did, for tests. */
void movie_stats(int *shown, int *late, int *decode_us_max);
int movie_last_error(void);
void movie_timing(unsigned int *elapsed_ms, unsigned int *audio_ms);

#endif
